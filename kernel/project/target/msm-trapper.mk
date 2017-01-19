# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# main project for qemu-aarch64
ARCH := arm64
TARGET := msm-trapper
ARM_CPU := cortex-a53

# include fastboot header in start.S
KERNEL_DEFINES += FASTBOOT_HEADER=1

DEVICE_TREE := kernel/target/msm-trapper/device-tree.dtb
OUTLKZIMAGE := $(BUILDDIR)/z$(LKNAME).bin
OUTLKZIMAGE_DTB := $(OUTLKZIMAGE)-dtb

# rule for gzipping the kernel
$(OUTLKZIMAGE): $(OUTLKBIN)
	@echo gzipping image: $@
	$(NOECHO)gzip -c $< > $@

# rule for appending device tree
$(OUTLKZIMAGE_DTB): $(OUTLKZIMAGE)
	@echo concatenating device tree: $@
	$(NOECHO)cat $< $(DEVICE_TREE) > $@

# build gzipped kernel with concatenated device tree
all:: $(OUTLKZIMAGE_DTB)
