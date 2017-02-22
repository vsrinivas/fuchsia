# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

SUBDIR_INCLUDES := \
    $(LOCAL_DIR)/bootserver/build.mk \
    $(LOCAL_DIR)/loglistener/build.mk \
    $(LOCAL_DIR)/mdi/build.mk \
    $(LOCAL_DIR)/mkbootfs/build.mk \
    $(LOCAL_DIR)/netprotocol/build.mk \
    $(LOCAL_DIR)/sysgen/build.mk \

include $(SUBDIR_INCLUDES)

# Export these files for use when building host tools from outside of magenta.
# Since these files are for use in building host tools, here is as good a
# place as any to do this for now.
PUBLIC_HEADER_DIR := system/public
EXPORTED_HEADERS := \
    $(BUILDDIR)/tools/include/magenta/compiler.h \
    $(BUILDDIR)/tools/include/magenta/ktrace-def.h \
    $(BUILDDIR)/tools/include/magenta/ktrace.h \

$(BUILDDIR)/tools/include/%: $(PUBLIC_HEADER_DIR)/%
	@echo exporting $<
	@$(MKDIR)
	$(NOECHO)cp -pf $< $@

GENERATED += $(EXPORTED_HEADERS)
EXTRA_BUILDDEPS += $(EXPORTED_HEADERS)
