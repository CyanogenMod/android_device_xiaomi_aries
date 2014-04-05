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

#include <utils/Errors.h>
#include <sound/es310.h>
#include <utils/threads.h>
#include <sys/system_properties.h>
#include <AudioHardwareALSA.h>

int amplifier_open(void);
void amplifier_set_devices(int devices);
int amplifier_set_mode(audio_mode_t mode);
int amplifier_close(void);

extern int fBoot;
extern int mAudienceCmd;
extern pthread_cond_t mAudienceCV;
extern int mLoopbackState;
extern android::Mutex mAudioCodecLock;
extern bool    mAudienceCodecInit;

extern int mPrevDevice;
extern pthread_mutex_t m_csd_mutex;
extern pthread_cond_t m_csd_cv;
extern bool m_killcsdThread;
extern int m_csdCmd;
extern pthread_t csdThread;
extern void *csdThreadWrapper(void *me);

enum {
    CMD_CSD_READY = -1,
    CMD_CSD_START_VOICE = 0,
    CMD_CSD_END_VOICE    = 1,
    CMD_CSD_ENABLE_DEVICE = 2,
    CMD_CSD_DISABLE_DEVICE = 3,
};

enum {
    CMD_AUDIENCE_READY = -1,
    CMD_AUDIENCE_WAKEUP = 0,
};

typedef android::status_t (android_audio_legacy::ALSADevice::*ALSADevice_setMixerControl1)(const char *name, const char *);
typedef android::status_t (android_audio_legacy::ALSADevice::*ALSADevice_setMixerControl2)(const char *name, unsigned int value, int index);

void enableAudienceloopback(int enable);
void *AudienceThreadWrapper(void *me);
android::status_t doAudienceCodec_Init(android_audio_legacy::ALSADevice *alsadev, ALSADevice_setMixerControl1 c1, ALSADevice_setMixerControl2 c2);
android::status_t doAudienceCodec_DeInit(void);
android::status_t doAudienceCodec_Wakeup();
android::status_t doRouting_Audience_Codec(int mode, int device, bool enable);
const char* getNameByPresetID(int presetID);






