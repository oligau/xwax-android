include $(call all-subdir-makefiles)

APP_PLATFORM := android-18

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := main

SDL_PATH := ../SDL
SDLttf_PATH := ../SDLtff
tinyalsa_PATH := ../tinyalsa
XWAX_PATH := ./

LOCAL_C_INCLUDES := $(LOCAL_PATH)/$(SDL_PATH)/include \
	$(LOCAL_PATH)/$(SDLttf_PATH) \
	$(LOCAL_PATH)/$(tinyalsa_PATH)/include/tinyalsa

# Add your application source files here...
LOCAL_SRC_FILES := $(SDL_PATH)/src/main/android/SDL_android_main.c \
	$(XWAX_PATH)/xwax.c \
	$(XWAX_PATH)/tinyalsa.c \
	$(XWAX_PATH)/controller.c \
	$(XWAX_PATH)/cues.c \
	$(XWAX_PATH)/deck.c \
	$(XWAX_PATH)/device.c \
	$(XWAX_PATH)/external.c \
	$(XWAX_PATH)/interface.c \
	$(XWAX_PATH)/library.c \
	$(XWAX_PATH)/listing.c \
	$(XWAX_PATH)/lut.c \
	$(XWAX_PATH)/player.c \
	$(XWAX_PATH)/realtime.c \
	$(XWAX_PATH)/rig.c \
	$(XWAX_PATH)/selector.c \
	$(XWAX_PATH)/status.c \
	$(XWAX_PATH)/thread.c \
	$(XWAX_PATH)/timecoder.c \
	$(XWAX_PATH)/track.c

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_ttf tinyalsa

LOCAL_LDLIBS := -lGLESv1_CM -llog

include $(BUILD_SHARED_LIBRARY)
