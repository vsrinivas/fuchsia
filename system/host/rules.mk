# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

# Export these files for use when building host tools from outside of magenta.
# Since these files are for use in building host tools, here is as good a
# place as any to do this for now.
PUBLIC_HEADER_DIR := system/public
EXPORTED_HEADERS := \
    $(BUILDDIR)/tools/include/magenta/compiler.h \
    $(BUILDDIR)/tools/include/magenta/errors.h \
    $(BUILDDIR)/tools/include/magenta/ktrace-def.h \
    $(BUILDDIR)/tools/include/magenta/ktrace.h \
    $(BUILDDIR)/tools/include/magenta/syscalls/object.h \
    $(BUILDDIR)/tools/include/magenta/types.h \

$(BUILDDIR)/tools/include/%: $(PUBLIC_HEADER_DIR)/%
	$(call BUILDECHO,exporting $<)
	@$(MKDIR)
	$(NOECHO)cp -pf $< $@

GENERATED += $(EXPORTED_HEADERS)
EXTRA_BUILDDEPS += $(EXPORTED_HEADERS)


HOSTAPPS := \
	$(LOCAL_DIR)/bootserver/rules.mk \
	$(LOCAL_DIR)/loglistener/rules.mk \
	$(LOCAL_DIR)/mdi/rules.mk \
	$(LOCAL_DIR)/merkleroot/rules.mk \
	$(LOCAL_DIR)/mkbootfs/rules.mk \
	$(LOCAL_DIR)/mkfs-msdosfs/rules.mk \
	$(LOCAL_DIR)/netprotocol/rules.mk \
	$(LOCAL_DIR)/sysgen/rules.mk \

include $(HOSTAPPS)
