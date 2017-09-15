# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# this file contains extra build rules for building various kernel
#  package configurations

OUT_ZIRCON_IMAGE := $(BUILDDIR)/$(LKNAME).bin

OUT_ZIRCON_ZIMAGE := $(BUILDDIR)/z$(LKNAME).bin
OUT_ZIRCON_ZIMAGE_DTB := $(OUT_ZIRCON_ZIMAGE)-dtb
OUT_ZIRCON_IMAGE_DTB := $(OUT_ZIRCON_IMAGE)-dtb
OUT_ZIRCON_ZIMAGE_KDTB := $(BUILDDIR)/z$(LKNAME).kdtb


GENERATED += $(OUT_ZIRCON_ZIMAGE) \
             $(OUT_ZIRCON_ZIMAGE_DTB) \
             $(OUT_ZIRCON_IMAGE_DTB) \
             $(OUT_ZIRCON_ZIMAGE_KDTB)

KERNSIZE=`stat -c%s "$(OUT_ZIRCON_ZIMAGE)"`
DTBSIZE=`stat -c%s "$(DEVICE_TREE)"`

KDTBTOOL=$(BUILDDIR)/tools/mkkdtb

# rule for gzipping the kernel
$(OUT_ZIRCON_ZIMAGE): $(OUTLKBIN)
	$(call BUILDECHO,gzipping image $@)
	$(NOECHO)gzip -c $< > $@

# rule for appending device tree, compressed kernel
$(OUT_ZIRCON_ZIMAGE_DTB): $(OUT_ZIRCON_ZIMAGE) $(DEVICE_TREE)
	$(call BUILDECHO,concatenating device tree $@)
	$(NOECHO)cat $< $(DEVICE_TREE) > $@

# rule for appending device tree, uncompressed kernel
$(OUT_ZIRCON_IMAGE_DTB): $(OUTLKBIN) $(DEVICE_TREE)
	$(call BUILDECHO,concatenating device tree $@)
	$(NOECHO)cat $< $(DEVICE_TREE) > $@

$(OUT_ZIRCON_ZIMAGE_KDTB): $(OUT_ZIRCON_ZIMAGE) $(DEVICE_TREE) $(KDTBTOOL)
	$(KDTBTOOL) $(OUT_ZIRCON_ZIMAGE) $(DEVICE_TREE) $@

# build gzipped kernel with concatenated device tree
all:: $(OUT_ZIRCON_ZIMAGE_DTB) $(OUT_ZIRCON_IMAGE_DTB) $(OUT_ZIRCON_ZIMAGE_KDTB)
