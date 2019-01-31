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
    $(LOCAL_DIR)/userboot-image.S \

MODULE_DEPS := kernel/lib/vdso

userboot-filename := $(BUILDDIR)/system/core/userboot/libuserboot.so

# userboot-image.S embeds this file, so building depends on it.
MODULE_SRCDEPS += $(userboot-filename).strip

# This generated header file tells the userboot.cpp code
# where the segment boundaries and entry points are.
MODULE_SRCDEPS += $(BUILDDIR)/$(LOCAL_DIR)/userboot-code.h
$(BUILDDIR)/$(LOCAL_DIR)/userboot-code.h: scripts/gen-rodso-code.sh $(userboot-filename)
	@$(MKDIR)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(SHELLEXEC) $< '$(NM)' '$(READELF)' \
	    $@.new USERBOOT $(userboot-filename)
	@mv -f $@.new $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/userboot-code.h
MODULE_COMPILEFLAGS += -I$(BUILDDIR)/$(LOCAL_DIR)

include make/module.mk
