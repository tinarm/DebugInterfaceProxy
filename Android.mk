LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	main.c \
	autoconf.c \
	cmdserver.c \
	mldproc.c \
	tracecmd.c \
	utils.c

LOCAL_C_INCLUDES:= $(call include-path-for, dbus)

LOCAL_SHARED_LIBRARIES:= \
	libutils \
	libcutils

LOCAL_CFLAGS:= -fno-short-enums -Wall -DANDROID_OS

LOCAL_MODULE:= debug_interface_proxy
LOCAL_MODULE_TAGS:= optional

include $(BUILD_EXECUTABLE)

