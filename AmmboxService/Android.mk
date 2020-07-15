LOCAL_PATH:= $(call my-dir)


include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-subdir-java-files) $(call all-Iaidl-files-under, src)
LOCAL_AIDL_INCLUDES := $(call all-Iaidl-files-under, src)

LOCAL_PACKAGE_NAME := AmmboxService
LOCAL_CERTIFICATE := platform

LOCAL_JNI_SHARED_LIBRARIES := libAmmbox
LOCAL_REQUIRED_MODULES := libAmmbox

include $(BUILD_PACKAGE)
