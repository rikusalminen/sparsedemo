LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := ndk-skeleton
LOCAL_CFLAGS=-std=gnu99 -W -Wall -Wextra -Werror
LOCAL_CFLAGS+=-I$(LOCAL_PATH)/khronos
LOCAL_CFLAGS+=-g
LOCAL_SRC_FILES=main.c glxw.c
LOCAL_LDLIBS=-landroid -llog -lEGL -lGLESv2

include $(BUILD_SHARED_LIBRARY)
