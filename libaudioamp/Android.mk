LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	liblog libutils libcutils libdl

LOCAL_SRC_FILES := \
	audio_amplifier.cpp

LOCAL_MODULE := libaudioamp

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += hardware/qcom/audio-caf/legacy/alsa_sound
LOCAL_C_INCLUDES += hardware/qcom/audio-caf/legacy/libalsa-intf

include $(BUILD_SHARED_LIBRARY)
