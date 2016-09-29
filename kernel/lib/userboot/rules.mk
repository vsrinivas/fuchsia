# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
    $(LOCAL_DIR)/userboot.cpp \
    $(LOCAL_DIR)/vdso.S

ifeq ($(call TOBOOL,$(EMBED_USER_BOOTFS)),true)
MODULE_SRCDEPS += $(USER_BOOTFS)
MODULE_DEFINES += EMBED_USER_BOOTFS=1
MODULE_COMPILEFLAGS += -DUSER_BOOTFS_FILENAME="\"$(USER_BOOTFS)\""
MODULE_SRCS += $(LOCAL_DIR)/bootfs.S
endif

vdso-filename := $(BUILDDIR)/ulib/magenta/libmagenta.so
userboot-filename := $(BUILDDIR)/core/userboot/libuserboot.so

# vdso.S embeds those two files, so building it depends on them.
MODULE_SRCDEPS += $(vdso-filename).strip $(userboot-filename).strip
MODULE_COMPILEFLAGS += \
    -DVDSO_FILENAME='"$(vdso-filename).strip"' \
    -DUSERBOOT_FILENAME='"$(userboot-filename).strip"'

# This generated header file tells the userboot.cpp code
# where the segment boundaries and entry points are.
MODULE_SRCDEPS += $(BUILDDIR)/$(LOCAL_DIR)/code-start.h
$(BUILDDIR)/$(LOCAL_DIR)/code-start.h: \
    scripts/gen-rodso-code.sh $(vdso-filename) $(userboot-filename)
	@$(MKDIR)
	@echo generating $@
	$(NOECHO)$< '$(NM)' \
	    VDSO $(BUILDDIR)/ulib/magenta/libmagenta.so \
	    USERBOOT $(BUILDDIR)/core/userboot/libuserboot.so \
	    > $@.new
	@mv -f $@.new $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/code-start.h
MODULE_COMPILEFLAGS += -I$(BUILDDIR)/$(LOCAL_DIR)

include make/module.mk
