#
# Copyright 2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifneq ($(filter aries aries,$(TARGET_DEVICE)),)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

$(shell mkdir -p $(OUT)/obj/SHARED_LIBRARIES/libqminvapi_intermediates)
$(shell touch $(OUT)/obj/SHARED_LIBRARIES/libqminvapi_intermediates/export_includes)

LOCAL_SRC_FILES := addrloader.c
LOCAL_SHARED_LIBRARIES := libqminvapi liblog libcutils liblogwrap
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_OWNER := xiaomi
LOCAL_MODULE := wifiAddrLoader
include $(BUILD_EXECUTABLE)

endif
