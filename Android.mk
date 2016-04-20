LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := versioner
LOCAL_MODULE_HOST_OS := linux

LOCAL_CLANG := true
LOCAL_RTTI_FLAG := -fno-rtti
LOCAL_CFLAGS := -Wall -Wextra -Wno-unused-parameter
LOCAL_CFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -fno-rtti
LOCAL_CPPFLAGS := $(LOCAL_CFLAGS) -std=c++14

LOCAL_SRC_FILES := src/versioner.cpp src/HeaderDatabase.cpp src/Utils.cpp
LOCAL_SHARED_LIBRARIES := libclang libLLVM libbase

include $(BUILD_HOST_EXECUTABLE)
