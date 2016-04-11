LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := unique_decl
LOCAL_MODULE_HOST_OS := linux

LOCAL_CLANG := true
LOCAL_RTTI_FLAG := -fno-rtti
LOCAL_CFLAGS := -Wall -Wextra -Wno-unused-parameter
LOCAL_CFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -fno-rtti
LOCAL_CPPFLAGS := $(LOCAL_CFLAGS) -std=c++14

LOCAL_SRC_FILES := src/unique_decl.cpp
LOCAL_SHARED_LIBRARIES := libclang libLLVM

include $(BUILD_HOST_EXECUTABLE)
