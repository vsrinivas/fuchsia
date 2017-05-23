# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
    $(LOCAL_DIR)/rodso.cpp \
    $(LOCAL_DIR)/vdso.cpp \
    $(LOCAL_DIR)/vdso-image.S \

MODULE_DEPS := \
    kernel/lib/mxtl \

vdso-filename := $(BUILDDIR)/system/ulib/magenta/libmagenta.so

# vdso-image.S embeds this file, so building depends on it.
MODULE_SRCDEPS += $(vdso-filename).strip

# This generated header file tells the vdso.cpp code
# where the segment boundaries and entry points are.
MODULE_SRCDEPS += $(BUILDDIR)/$(LOCAL_DIR)/vdso-code.h
$(BUILDDIR)/$(LOCAL_DIR)/vdso-code.h: scripts/gen-rodso-code.sh $(vdso-filename)
	@$(MKDIR)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(SHELLEXEC) $< '$(NM)' $@.new VDSO $(vdso-filename)
	@mv -f $@.new $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-code.h
MODULE_COMPILEFLAGS += -I$(BUILDDIR)/$(LOCAL_DIR)

MODULE_SRCDEPS += $(BUILDDIR)/$(LOCAL_DIR)/vdso-valid-sysret.h
$(BUILDDIR)/$(LOCAL_DIR)/vdso-valid-sysret.h: \
    scripts/gen-vdso-valid-sysret.sh $(BUILDDIR)/$(LOCAL_DIR)/vdso-code.h
	@$(MKDIR)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(SHELLEXEC) $^ > $@.new
	@mv -f $@.new $@
GENERATED += $(BUILDDIR)/$(LOCAL_DIR)/vdso-valid-sysret.h

include make/module.mk
