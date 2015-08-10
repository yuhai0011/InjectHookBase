LOCAL_PATH := $(call my-dir)

HOOK_SHARED_LIBS := \
	liblog \
	libcutils \
	libutils \
	libandroid_runtime \
	libdvm


include $(CLEAR_VARS)
LOCAL_SHARED_LIBRARIES := $(HOOK_SHARED_LIBS)
LOCAL_MODULE    := dvmhook
LOCAL_ARM_MODE	:= thumb
LOCAL_CFLAGS	:= -std=gnu++11 -fpermissive -DDEBUG -O0
LOCAL_SRC_FILES := \
	JavaHook/JavaMethodHook.cpp \
	JavaHook/DalvikMethodHook.cpp \
	JavaHook/HookBroadcastMethod.cpp \
	JavaHook/HookProviderMethod.cpp \
	JavaHook/HookServiceMethod.cpp \
	ElfHook/elfhook.cpp \
	ElfHook/elfio.cpp \
	ElfHook/elfutils.cpp \
	main.cpp
include $(BUILD_SHARED_LIBRARY)

