# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# this file contains extra build rules for building fastboot compatible image

# include fastboot header in start.S
KERNEL_DEFINES += FASTBOOT_HEADER=1

OUTLKIMAGE := $(BUILDDIR)/$(LKNAME).bin
OUTLKZIMAGE := $(BUILDDIR)/z$(LKNAME).bin
OUTLKZIMAGE_DTB := $(OUTLKZIMAGE)-dtb
OUTLKIMAGE_DTB := $(OUTLKIMAGE)-dtb

GENERATED += $(OUTLKZIMAGE) $(OUTLKZIMAGE_DTB)

# rule for gzipping the kernel
$(OUTLKZIMAGE): $(OUTLKBIN)
	$(call BUILDECHO,gzipping image $@)
	$(NOECHO)gzip -c $< > $@

# rule for appending device tree
$(OUTLKZIMAGE_DTB): $(OUTLKZIMAGE) $(DEVICE_TREE)
	$(call BUILDECHO,concatenating device tree $@)
	$(NOECHO)cat $< $(DEVICE_TREE) > $@

$(OUTLKIMAGE_DTB): $(OUTLKIMAGE) $(DEVICE_TREE)
	$(call BUILDECHO,concatenating device tree $@)
	$(NOECHO)cat $< $(DEVICE_TREE) > $@
# build gzipped kernel with concatenated device tree
all:: $(OUTLKZIMAGE_DTB) $(OUTLKIMAGE_DTB)
