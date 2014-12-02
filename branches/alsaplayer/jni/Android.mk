
LOCAL_PATH := $(call my-dir)

# common codecs & startup library
include $(CLEAR_VARS)
LOCAL_MODULE := lossless
LOCAL_STATIC_LIBRARIES := flac ape
LOCAL_CFLAGS += -march=armv7-a -O3 -Wall -finline-functions -fPIC -Iinclude 
LOCAL_CFLAGS += -DHAVE_CONFIG_H -DCLASS_NAME=\"net/avs234/alsaplayer/AndLessSrv\"
LOCAL_CFLAGS += -DBUILD_STANDALONE -DCPU_ARM
#LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := main.c alsa.c buffer.c
LOCAL_LDLIBS := -llog -ldl
include $(BUILD_SHARED_LIBRARY)

#include $(CLEAR_VARS)
#LOCAL_MODULE := flactest
#LOCAL_STATIC_LIBRARIES := flac
#LOCAL_CFLAGS += -O3 -Wall -finline-functions -fPIC -DHAVE_CONFIG_H -Iinclude
#LOCAL_SRC_FILES := flactest.c
#LOCAL_LDLIBS := -llog -ldl
#include $(BUILD_EXECUTABLE)

CODECS := flac ape
codec-makefiles =  $(patsubst %,$(LOCAL_PATH)/%/Android.mk,$(CODECS)) 
include $(call codec-makefiles)

