LOCAL_PATH := $(call my-dir)

INJECTOR_C_INCLUDES := \
	bionic/linker \
	$(JNI_H_INCLUDE)

INJECTOR_SHARED_LIBS := \
	libdl \
	libcutils \
	libutils \
	liblog

include $(CLEAR_VARS)
 
LOCAL_SHARED_LIBRARIES := $(INJECTOR_SHARED_LIBS)
LOCAL_C_INCLUDES := $(INJECTOR_C_INCLUDES)
LOCAL_MODULE := injector
LOCAL_SRC_FILES := inject.c
LOCAL_MODULE_TAGS := optional

$(info LOCAL_SRC_FILES=$(LOCAL_SRC_FILES).)
include $(BUILD_EXECUTABLE)  
