/*
 * Copyright (C) 2013, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <system/audio.h>

//#define LOG_NDEBUG 0
#define LOG_TAG "libaudioamp"
#include <cutils/log.h>

#include "audio_amplifier.h"

#include <utils/Errors.h>
#include <hardware/audio.h>
#include <utils/threads.h>
#include <cutils/properties.h>
#include <utils/List.h>
#include <utils/Mutex.h>
#include <dlfcn.h>
#include <sys/system_properties.h>
#include <AudioHardwareALSA.h>

enum {
    CMD_AUDIENCE_READY = -1,
    CMD_AUDIENCE_WAKEUP = 0,
};

// AMP API
int amplifier_open(void) {
    return 0;
}

void amplifier_set_devices(int devices) {
}

int amplifier_set_mode(audio_mode_t mode) {
    return 0;
}

int amplifier_close(void) {
    return 0;
}

// public: vars
pthread_cond_t mAudienceCV;
int mAudienceCmd;
bool    mAudienceCodecInit = 0;
android::Mutex mAudioCodecLock;
// public: functions
void enableAudienceloopback(int enable);
android::status_t doAudienceCodec_Init(android_audio_legacy::ALSADevice* alsa_device);
android::status_t doAudienceCodec_DeInit(void);
android::status_t doRouting_Audience_Codec(int mode, int device, bool enable);

// private: vars
static pthread_t AudienceThread;
static pthread_mutex_t mAudienceMutex;
static bool mKillAudienceThread;
static int mLoopbackState = 0;
static ES310_PathID dwOldPath = ES310_PATH_SUSPEND;
static ES310_PathID dwNewPath = ES310_PATH_SUSPEND;
static int AudiencePrevMode = AUDIO_MODE_NORMAL;
static unsigned int dwOldPreset = -1;
static unsigned int dwNewPreset = -1;
// private: functions
static android::status_t doAudienceCodec_Wakeup(void);
static const char* getNameByPresetID(int presetID);
static ALSADevice_setMixerControl1 setMixerControl1;
static ALSADevice_setMixerControl2 setMixerControl2;
static android_audio_legacy::ALSADevice* alsadevObj;


// Call this API after enabling the Audience, Also call this API before disabling the Audience
void enableAudienceloopback(int enable)
{
    if (mAudienceCodecInit != 1) {
        ALOGE("Audience Codec not initialized.\n");
        return;
    }

    if (mLoopbackState == enable)
        return;

    ALOGV("enableAudienceloopback enable:%d", enable);
    if(enable ==1 ) {
        (alsadevObj->*setMixerControl1)("IIR1 INP1 MUX", "DEC2");
    }
    else {
        (alsadevObj->*setMixerControl2)("RX1 Digital Volume",1,0);
        (alsadevObj->*setMixerControl2)("RX2 Digital Volume",1,0);
        (alsadevObj->*setMixerControl2)("RX3 Digital Volume",1,0);
        (alsadevObj->*setMixerControl2)("RX4 Digital Volume",1,0);
        (alsadevObj->*setMixerControl1)("IIR1 INP1 MUX", "ZERO");
    }

    mLoopbackState = enable;
}

static void AudienceThreadEntry() {
    ALOGV("AudioHardwareALSA::AudienceThreadEntry +");
    pid_t tid  = gettid();
    int err;
    androidSetThreadPriority(tid, ANDROID_PRIORITY_AUDIO);
    mAudienceCmd = CMD_AUDIENCE_READY;
    while(!mKillAudienceThread) {
        switch (mAudienceCmd)
        {
            case CMD_AUDIENCE_WAKEUP:
            {
                ALOGV("AudioHardwareALSA::AudienceThreadEntry, doAudienceCodec_Wakeup");
                err = doAudienceCodec_Wakeup();
                if (err < 0) {
                    ALOGE("doAudienceCodec_Wakeup: error %d\n", err);
                }
                break;
            }
            default:
                mAudienceCmd = CMD_AUDIENCE_READY;
        }
        mAudienceCmd = CMD_AUDIENCE_READY;
        pthread_mutex_lock(&mAudienceMutex);
        pthread_cond_wait(&mAudienceCV, &mAudienceMutex);
        pthread_mutex_unlock(&mAudienceMutex);
        ALOGV("Audience command:%d", mAudienceCmd);
        continue;
    }
    ALOGV("ALSADevice::csdThreadEntry -");
}

void *AudienceThreadWrapper(void *me) {
    AudienceThreadEntry();
    return NULL;
}

void tryWakeupAudience(void) {
	if (mAudienceCmd == CMD_AUDIENCE_READY)
    {
        mAudienceCmd = CMD_AUDIENCE_WAKEUP;
        pthread_cond_signal(&mAudienceCV);
    }
}

android::status_t doAudienceCodec_Init(android_audio_legacy::ALSADevice *alsadev, ALSADevice_setMixerControl1 c1, ALSADevice_setMixerControl2 c2)
{
    int fd_codec = -1;
    int rc = 0;
    int Audio_path = ES310_PATH_SUSPEND;
    int retry_count = 20;
    static const char *const path = "/dev/audience_es310";

	if(mAudienceCodecInit!=0)
		return 0;

	android::Mutex::Autolock lock(mAudioCodecLock);

	setMixerControl1 = c1;
	setMixerControl2 = c2;
	alsadevObj = alsadev;

    fd_codec = open("/dev/audience_es310", O_RDWR | O_NONBLOCK, 0);

    if (fd_codec < 0) {
        ALOGE("Cannot open %s %d.\n", path, fd_codec);
            return fd_codec;
    }

    while(retry_count)
    {
        ALOGV("start loading the voiceproc.img file, retry:%d +", 20 - retry_count);
        ALOGV("set codec reset command");
        rc = ioctl(fd_codec, ES310_RESET_CMD);
        if (rc != 0)
        {
            ALOGE("ES310_RESET_CMD fail, rc:%d", rc);
            retry_count--;
            continue;
        }
        ALOGV("start loading the voiceproc.img file -");
        usleep(11000);
        ALOGV("ES310 SYNC CMD +");
        rc = ioctl(fd_codec, ES310_SYNC_CMD, NULL);
        ALOGV("ES310 SYNC CMD, rc:%d-", rc);
        if (rc != 0)
        {
            ALOGE("ES310 SYNC CMD fail, rc:%d", rc);
            retry_count--;
            continue;
        }

        if (rc == 0)
            break;
    }

    pthread_mutex_init(&mAudienceMutex, NULL);
    pthread_cond_init (&mAudienceCV, NULL);
    mKillAudienceThread = false;
    mAudienceCmd = CMD_AUDIENCE_READY;
    ALOGV("Creating Audience Thread");
    pthread_create(&AudienceThread, NULL, AudienceThreadWrapper, NULL);

    if (rc == 0) {
        ALOGV("audience codec init OK\n");
        mAudienceCodecInit = 1;
    } else
        ALOGE("audience codec init failed\n");

    close(fd_codec);
    return rc;
}

android::status_t doAudienceCodec_DeInit(void)
{
    mKillAudienceThread = true;
    pthread_cond_signal(&mAudienceCV);
    pthread_join(AudienceThread,NULL);
    ALOGV("Audience Thread Killed");
    return 0;
}

static android::status_t doAudienceCodec_Wakeup()
{
    int fd_codec = -1;
    int retry = 4;
    int rc = 0;
    ALOGV("Pre Wakeup Audience Codec ++");
    android::Mutex::Autolock lock(mAudioCodecLock);
    if (fd_codec < 0) {
        fd_codec = open("/dev/audience_es310", O_RDWR);
        if (fd_codec < 0) {
            ALOGE("Cannot open either audience_es310 device (%d)\n", fd_codec);
            return -1;
        }
    }

    retry = 4;
    do {
        ALOGV("ioctl ES310_SET_CONFIG retry:%d", 4-retry);
        rc = ioctl(fd_codec, ES310_WAKEUP_CMD, NULL);
        if (rc == 0) {
            break;
        }
        else {
            ALOGE("ERROR: ES310_SET_CONFIG rc=%d", rc);
        }
    } while (--retry);

    close(fd_codec);
    fd_codec = -1;

    ALOGV("Pre Wakeup Audience Codec --");
    return rc;
}

android::status_t doRouting_Audience_Codec(int mode, int device, bool enable)
{
    int rc = 0;
    int retry = 4;
    int fd_codec = -1;
    bool bVideoRecord_NS = false;
    bool bPresetAgain = false;
    bool bForcePathAgain = false;
    bool bVRMode = false;
    char cVRMode[255]="0";
    char cVNRMode[255]="2";
    int VNRMode = 2;

    if (mAudienceCodecInit != 1) {
        ALOGE("Audience Codec not initialized.\n");
        return -1;
    }

    ALOGD("doRouting_Audience_Codec mode:%d Routes:0x%x Enable:%d.\n", mode, device, enable);

    android::Mutex::Autolock lock(mAudioCodecLock);
    if ((mode < AUDIO_MODE_CURRENT) || (mode >= AUDIO_MODE_CNT)) {
        ALOGW("Illegal value: doRouting_Audience_Codec(%d, 0x%x, %d)", mode, device, enable);
        return -1;
    }

    if (enable == 0)
    {
        dwNewPath = ES310_PATH_SUSPEND;
        goto ROUTE;
    }

    if (((device & AUDIO_DEVICE_IN_ALL) == 0) &&
        ((device & AUDIO_DEVICE_OUT_ALL) != 0) &&
        (mode == AUDIO_MODE_NORMAL))
    {
        ALOGV("doRouting_Audience_Codec: Normal mode, RX no routing\n");
        return 0;
    }

    property_get("audio.record.vrmode",cVRMode,"0");
    if (!strncmp("1", cVRMode, 1)) {
        bVRMode = 1;
    } else {
        bVRMode = 0;
    }

    property_get("persist.audio.vns.mode",cVNRMode,"2");
    if (!strncmp("1", cVNRMode, 1)) {
        VNRMode = 1;
    } else {
        VNRMode = 2;
    }

    ALOGV("doRouting_Audience_Codec  -> VRMode:%d, VNRMode:%d", bVRMode, VNRMode);

    if (mode == AUDIO_MODE_IN_CALL ||
         mode == AUDIO_MODE_RINGTONE) {
        switch (device & AUDIO_DEVICE_OUT_ALL) {
            case AUDIO_DEVICE_OUT_EARPIECE:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_INCALL_NB;
                 break;
            case AUDIO_DEVICE_OUT_SPEAKER:
                 dwNewPath = ES310_PATH_HANDSFREE;
                 dwNewPreset = ES310_PRESET_HANDSFREE_INCALL_NB;
                 break;
            case AUDIO_DEVICE_OUT_WIRED_HEADSET:
                 dwNewPath = ES310_PATH_HEADSET;
                 dwNewPreset = ES310_PRESET_HEADSET_INCALL_NB;
                 break;
            case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_INCALL_NB;
                 break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_INCALL_NB;
                 break;
            case AUDIO_DEVICE_OUT_AUX_DIGITAL:
            case AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET:
            case AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET:
                 dwNewPath = ES310_PATH_HEADSET;
                 dwNewPreset = ES310_PRESET_HEADSET_INCALL_NB;
                 break;
            default:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_INCALL_NB;
                 break;
        }
        goto ROUTE;
    }
    else if (mode == AUDIO_MODE_IN_COMMUNICATION) {
        switch (device & AUDIO_DEVICE_OUT_ALL) {
            case AUDIO_DEVICE_OUT_EARPIECE:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_VOIP_WB;
                 break;
            case AUDIO_DEVICE_OUT_SPEAKER:
                 dwNewPath = ES310_PATH_HANDSFREE;
                 dwNewPreset = ES310_PRESET_HANDSFREE_VOIP_WB;
                 break;
            case AUDIO_DEVICE_OUT_WIRED_HEADSET:
                 dwNewPath = ES310_PATH_HEADSET;
                 dwNewPreset = ES310_PRESET_HEADSET_VOIP_WB;
                 break;
            case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_VOIP_WB;
                 break;
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
            case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
            case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_VOIP_WB;
                 break;
            case AUDIO_DEVICE_OUT_AUX_DIGITAL:
            case AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET:
            case AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET:
                 dwNewPath = ES310_PATH_HEADSET;
                 dwNewPreset = ES310_PRESET_HEADSET_VOIP_WB;
                 break;
            default:
                 dwNewPath = ES310_PATH_HANDSET;
                 dwNewPreset = ES310_PRESET_HANDSET_VOIP_WB;
                 break;
        }
        goto ROUTE;
    }
    else {
        switch (device & AUDIO_DEVICE_IN_ALL)
        {
                //TX
                case AUDIO_DEVICE_IN_COMMUNICATION:
                     dwNewPath = ES310_PATH_HANDSFREE;
                     dwNewPreset = ES310_PRESET_HANDSFREE_REC_WB;
                     break;
                case AUDIO_DEVICE_IN_AMBIENT:
                     dwNewPath = ES310_PATH_HANDSFREE;
                     dwNewPreset = ES310_PRESET_HANDSFREE_REC_WB;
                     break;
                case AUDIO_DEVICE_IN_BUILTIN_MIC:
                     {
                         dwNewPath = ES310_PATH_HANDSET;
                         dwNewPreset = ES310_PRESET_ANALOG_BYPASS;
                     }
                     if (bVRMode)
                     {
                         dwNewPreset = ES310_PRESET_VOICE_RECOGNIZTION_WB;
                     }
                     break;
                case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET:
                     dwNewPath = ES310_PATH_HEADSET;
                     dwNewPreset = ES310_PRESET_HANDSFREE_REC_WB;
                     break;
                case AUDIO_DEVICE_IN_WIRED_HEADSET:
                     dwNewPath = ES310_PATH_HEADSET;
                     dwNewPreset = ES310_PRESET_HEADSET_MIC_ANALOG_BYPASS;
                     break;
                case AUDIO_DEVICE_IN_AUX_DIGITAL:
                case AUDIO_DEVICE_IN_VOICE_CALL:
                case AUDIO_DEVICE_IN_BACK_MIC:
                     dwNewPath = ES310_PATH_HANDSET;
                     dwNewPreset = ES310_PRESET_HANDSFREE_REC_WB;
                     break;
                default:
                     dwNewPath = ES310_PATH_HANDSET;
                     dwNewPreset = ES310_PRESET_HANDSFREE_REC_WB;
                     break;
        }
    }

ROUTE:

    if (VNRMode == 1) {
        ALOGE("Switch to 1-Mic Solution");
        if (dwNewPreset == ES310_PRESET_HANDSET_INCALL_NB) {
            dwNewPreset = ES310_PRESET_HANDSET_INCALL_NB_1MIC;
        }
        if (dwNewPreset == ES310_PRESET_HANDSET_VOIP_WB) {
            dwNewPreset = ES310_PRESET_HANDSET_INCALL_VOIP_WB_1MIC;
        }
    }

    ALOGV("doRouting_Audience_Codec: dwOldPath=%d, dwNewPath=%d, prevMode=%d, mode=%d",
                dwOldPath, dwNewPath, AudiencePrevMode, mode);

    if (AudiencePrevMode != mode)
    {
        bForcePathAgain = true;
        AudiencePrevMode = mode;
    }

    if (bForcePathAgain ||
        (dwOldPath != dwNewPath) ||
        (dwOldPreset != dwNewPreset)) {

        if (fd_codec < 0) {
            fd_codec = open("/dev/audience_es310", O_RDWR);
            if (fd_codec < 0) {
                ALOGE("Cannot open either audience_es310 device (%d)\n", fd_codec);
                return -1;
            }
        }

        if (bForcePathAgain ||
            (dwOldPath != dwNewPath)) {
            bPresetAgain = true;
            retry = 4;
            do {
                ALOGV("ioctl ES310_SET_CONFIG newPath:%d, retry:%d", dwNewPath, (4-retry));
                rc = ioctl(fd_codec, ES310_SET_CONFIG, &dwNewPath);

                if (rc == 0) {
                    dwOldPath = dwNewPath;
                    break;
                }
                else
                {
                    ALOGE("ERROR: ES310_SET_CONFIG rc=%d", rc);
                }
            } while (--retry);
            /*Close driver first incase we need to do audience HW reset when ES310_SET_CONFIG failed.*/
        }

        if (bPresetAgain)
            ALOGV("doRouting_Audience_Codec: dwOldPreset:%s, dwNewPreset:%s", getNameByPresetID(dwOldPreset), getNameByPresetID(dwNewPreset));

        if (bPresetAgain && (dwNewPath != ES310_PATH_SUSPEND)) {
            retry = 4;
            do {
                ALOGV("ioctl ES310_SET_PRESET newPreset:0x%x, retry:%d", dwNewPreset, (4-retry));
                rc = ioctl(fd_codec, ES310_SET_PRESET, &dwNewPreset);

                if (rc == 0) {
                    dwOldPreset = dwNewPreset;
                    break;
                }
                else
                {
                    ALOGE("ERROR: ES310_SET_PRESET rc=%d", rc);
                }
            } while (--retry);
            /*Close driver first incase we need to do audience HW reset when ES310_SET_CONFIG failed.*/
        }

        close(fd_codec);
        fd_codec = -1;

RECOVER:
        if (rc < 0) {
            ALOGE("E310 do hard reset to recover from error!\n");
            rc = doAudienceCodec_Init(alsadevObj, setMixerControl1, setMixerControl2); /* A1026 needs to do hard reset! */
            if (!rc) {
                fd_codec = open("/dev/audience_es310", O_RDWR);
                if (fd_codec >= 0) {
                    rc = ioctl(fd_codec, ES310_SET_CONFIG, &dwNewPath);
                    if (rc == android::NO_ERROR)
                        dwOldPath = dwNewPath;
                    else
                        ALOGE("Audience Codec Fatal Error! rc %d\n", rc);
                    close(fd_codec);
                } else
                    ALOGE("Audience Codec Fatal Error: Re-init Audience Codec open driver fail!! rc %d\n", fd_codec);
            } else
                ALOGE("Audience Codec Fatal Error: Re-init A1026 Failed. rc %d\n", rc);
        }
    }

    return android::NO_ERROR;
}
static const char* getNameByPresetID(int presetID)
{
     switch(presetID){
        case ES310_PRESET_HANDSET_INCALL_NB:
              return "ES310_PRESET_HANDSET_INCALL_NB";
        case ES310_PRESET_HEADSET_INCALL_NB:
              return "ES310_PRESET_HEADSET_INCALL_NB";
        case ES310_PRESET_HANDSET_INCALL_NB_1MIC:
              return "ES310_PRESET_HANDSET_INCALL_NB_1MIC";
        case ES310_PRESET_HANDSFREE_INCALL_NB:
              return "ES310_PRESET_HANDSFREE_INCALL_NB";
        case ES310_PRESET_HANDSET_INCALL_WB:
              return "ES310_PRESET_HANDSET_INCALL_WB";
        case ES310_PRESET_HEADSET_INCALL_WB:
              return "ES310_PRESET_HEADSET_INCALL_WB";
        case ES310_PRESET_AUDIOPATH_DISABLE:
              return "ES310_PRESET_AUDIOPATH_DISABLE";
        case ES310_PRESET_HANDSFREE_INCALL_WB:
              return "ES310_PRESET_HANDSFREE_INCALL_WB";
        case ES310_PRESET_HANDSET_VOIP_WB:
              return "ES310_PRESET_HANDSET_VOIP_WB";
        case ES310_PRESET_HEADSET_VOIP_WB:
              return "ES310_PRESET_HEADSET_VOIP_WB";
        case ES310_PRESET_HANDSFREE_REC_WB:
              return "ES310_PRESET_HANDSFREE_REC_WB";
        case ES310_PRESET_HANDSFREE_VOIP_WB:
              return "ES310_PRESET_HANDSFREE_VOIP_WB";
        case ES310_PRESET_VOICE_RECOGNIZTION_WB:
              return "ES310_PRESET_VOICE_RECOGNIZTION_WB";
        case ES310_PRESET_HANDSET_INCALL_VOIP_WB_1MIC:
              return "ES310_PRESET_HANDSET_INCALL_VOIP_WB_1MIC";
        case ES310_PRESET_ANALOG_BYPASS:
              return "ES310_PRESET_ANALOG_BYPASS";
        case ES310_PRESET_HEADSET_MIC_ANALOG_BYPASS:
              return "ES310_PRESET_HEADSET_MIC_ANALOG_BYPASS";
        default:
            return "Unknown";
     }
     return "Unknown";
}

