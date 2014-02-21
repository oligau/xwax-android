LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

SDL_PATH := ../SDL
XWAX_PATH := ../xwax-1.4

LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include

# Add your application source files here...
LOCAL_SRC_FILES := $(SDL_PATH)/src/main/android/SDL_android_main.c \
	$(XWAX_PATH)/xwax.c \
	$(XWAX_PATH)/alsa.c \
	$(XWAX_PATH)/controller.c \
	$(XWAX_PATH)/cues.c \
	$(XWAX_PATH)/deck.c \
	$(XWAX_PATH)/device.c \
	$(XWAX_PATH)/dicer.c \
	$(XWAX_PATH)/external.c \
	$(XWAX_PATH)/interface.c \
	$(XWAX_PATH)/library.c \
	$(XWAX_PATH)/listing.c \
	$(XWAX_PATH)/lut.c \
	$(XWAX_PATH)/midi.c \
	$(XWAX_PATH)/player.c \
	$(XWAX_PATH)/realtime.c \
	$(XWAX_PATH)/rig.c \
	$(XWAX_PATH)/selector.c \
	$(XWAX_PATH)/status.c \
	$(XWAX_PATH)/thread.c \
	$(XWAX_PATH)/timecoder.c \
	$(XWAX_PATH)/track.c

LOCAL_SHARED_LIBRARIES := SDL2

LOCAL_LDLIBS := -lGLESv1_CM -llog

include $(BUILD_SHARED_LIBRARY)
