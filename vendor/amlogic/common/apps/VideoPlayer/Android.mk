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

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := VideoPlayer
LOCAL_CERTIFICATE := platform
LOCAL_JAVA_LIBRARIES := droidlogic
LOCAL_STATIC_JAVA_LIBRARIES := android-support-v4

LOCAL_DEX_PREOPT := false

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26 && echo OK),OK)
LOCAL_PROPRIETARY_MODULE := true
LOCAL_JNI_SHARED_LIBRARIES += libsubtitlemanager_jni
endif

#LOCAL_REQUIRED_MODULES := libamplayerjni libsubjni
#LOCAL_PROGUARD_ENABLED := disabled
#LOCAL_PROGUARD_FLAGS := -include $(LOCAL_PATH)/proguard.flags

ifndef PRODUCT_SHIPPING_API_LEVEL
LOCAL_PRIVATE_PLATFORM_APIS := true
endif

include $(BUILD_PACKAGE)
##################################################

include $(call all-makefiles-under,$(LOCAL_PATH))
