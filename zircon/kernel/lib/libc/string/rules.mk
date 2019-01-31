# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

C_STRING_OPS := \
	memchr \
	memcmp \
	memcpy \
	memmove \
	memset \
	strcat \
	strchr \
	strcmp \
	strcoll \
	strcpy \
	strlcat \
	strlcpy \
	strlen \
	strncat \
	strncpy \
	strncmp \
	strnicmp \
	strnlen \
	strpbrk \
	strrchr \
	strspn \
	strstr \
	strtok \
	strxfrm

LIBC_STRING_C_DIR := $(LOCAL_DIR)

# include the arch specific string routines
#
# the makefile may filter out implemented versions from the C_STRING_OPS variable
include $(LOCAL_DIR)/arch/$(ARCH)/rules.mk

MODULE_SRCS += \
	$(addprefix $(LIBC_STRING_C_DIR)/,$(addsuffix .c,$(C_STRING_OPS)))

