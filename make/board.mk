# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# ***** README ******
#
# The following variables can be set in kernel/target/arm64/board/<board>/rules.mk
# to customize the zircon boot image produced for a board:
#
# PLATFORM_USE_SHIM := true             # enable building a zircon image with a board name
#
# PLATFORM_BOARD_NAME := <board-name>   # required if using the boot shim
#
# PLATFORM_USE_GZIP := true             # set if bootloader expects a gzipped image
#
# PLATFORM_USE_MKBOOTIMG := true        # package the zircon image with the Android mkbootimg tool
#
# PLATFORM_DTB_PATH := <path>           # set if bootloader requires a custom device tree (optional)
#                                       # common dummy dtb used if none specified
#
# PLATFORM_DTB_TYPE := append           # set if bootloader expects dtb appended to the image
#
# PLATFORM_DTB_TYPE := kdtb             # set if bootloader expects dtb packaged in kdtb format
#
# PLATFORM_DTB_TYPE := mkbootimg        # set if bootloader expects dtb added via mkbootimg --second
#                                         (requires PLATFORM_DTB_OFFSET)
#
# PLATFORM_DTB_OFFSET := <offset>       # DTB offset passed to mkbootimg
#                                         (required if using PLATFORM_DTB_TYPE := mkbootimg)
#
# PLATFORM_KERNEL_OFFSET := <offset>    # kernel offset to pass to mkbootimg
#
# PLATFORM_MEMBASE := <membase>         # memory base address to pass to mkbootimg
#
# PLATFORM_CMDLINE := <cmdline>         # kernel command line to pass to mkbootimg
#
# PLATFORM_BOOT_PARTITION_SIZE := <size> # boot partition size to pass to avbtool
#
# PLATFORM_USE_AVB := true              # set to sign image with avbtool and generate a vbmeta image

# Rules for building boot shim and prepending it to ZBI.
ifeq ($(PLATFORM_USE_SHIM),true)

ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif

BOARD_ZIRCON_BOOTIMAGE := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.bin
KDTBTOOL=$(BUILDDIR)/tools/mkkdtb

ifeq ($(TARGET),arm64)
include kernel/target/arm64/boot-shim/rules.mk
else
$(error PLATFORM_USE_SHIM not supported for target $(TARGET))
endif

ifeq ($(PLATFORM_DTB_PATH),)
PLATFORM_DTB_PATH := kernel/target/arm64/dtb/dummy-device-tree.dtb
endif

# capture board specific variables for the build rules
$(BOARD_ZIRCON_BOOTIMAGE): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)
$(BOARD_ZIRCON_BOOTIMAGE): BOOT_SHIM_BIN:=$(BOOT_SHIM_BIN)

# prepend shim to the zircon bootimage
$(BOARD_ZIRCON_BOOTIMAGE): $(ZIRCON_BOOTIMAGE) $(BOOT_SHIM_BIN)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)cat $(BOOT_SHIM_BIN) $(ZIRCON_BOOTIMAGE) > $@

GENERATED += $(BOARD_ZIRCON_BOOTIMAGE)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON_BOOTIMAGE)

endif # PLATFORM_USE_SHIM

# Rules for building an Android style boot.img containing the boot shim and ZBI.
# Bootloaders often require that the image be gzip compressed and followed by a
# Linux device tree binary, so a dummy dtb is concatenated at the end of the
# compressed image.
ifeq ($(PLATFORM_USE_MKBOOTIMG),true)

ifneq ($(PLATFORM_USE_SHIM),true)
$(error PLATFORM_USE_MKBOOTIMG requires PLATFORM_USE_SHIM)
endif
ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif
ifeq ($(PLATFORM_KERNEL_OFFSET),)
$(error PLATFORM_KERNEL_OFFSET not defined)
endif
ifeq ($(PLATFORM_MEMBASE),)
$(error PLATFORM_MEMBASE not defined)
endif
ifeq ($(PLATFORM_CMDLINE),)
$(error PLATFORM_CMDLINE not defined)
endif

MKBOOTIMG := third_party/tools/android/mkbootimg
MKBOOTFS_ARGS :=

BOARD_BOOTIMG := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-boot.img

ifeq ($(PLATFORM_USE_GZIP),true)
BOARD_ZIRCON_BOOTIMAGE2 := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.gz
else
BOARD_ZIRCON_BOOTIMAGE2 := $(BOARD_ZIRCON_BOOTIMAGE)
endif

ifeq ($(PLATFORM_DTB_TYPE),append)
# device tree binary is appended to end of zircon image
BOARD_ZIRCON_BOOTIMAGE3 := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage-dtb
else ifeq ($(PLATFORM_DTB_TYPE),kdtb)
# device tree binary is appended with the mkkdtb tool
BOARD_ZIRCON_BOOTIMAGE3 := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage-kdtb
else ifeq ($(PLATFORM_DTB_TYPE),mkbootimg)
# device tree binary passed via mkbootimg
ifeq ($(PLATFORM_DTB_OFFSET),)
$(error PLATFORM_DTB_OFFSET not defined)
endif
BOARD_ZIRCON_BOOTIMAGE3 := $(BOARD_ZIRCON_BOOTIMAGE2)
MKBOOTFS_ARGS += --second $(PLATFORM_DTB_PATH) --second_offset $(PLATFORM_DTB_OFFSET)
else
# no device tree is necessary
BOARD_ZIRCON_BOOTIMAGE3 := $(BOARD_ZIRCON_BOOTIMAGE2)
endif

$(BOARD_ZIRCON_BOOTIMAGE2): BOARD_ZIRCON_BOOTIMAGE2:=$(BOARD_ZIRCON_BOOTIMAGE2)
$(BOARD_ZIRCON_BOOTIMAGE2): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)

$(BOARD_ZIRCON_BOOTIMAGE3): BOARD_ZIRCON_BOOTIMAGE3:=$(BOARD_ZIRCON_BOOTIMAGE3)
$(BOARD_ZIRCON_BOOTIMAGE3): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)
$(BOARD_ZIRCON_BOOTIMAGE3): BOARD_ZIRCON_BOOTIMAGE2:=$(BOARD_ZIRCON_BOOTIMAGE2)
$(BOARD_ZIRCON_BOOTIMAGE3): PLATFORM_DTB_PATH:=$(PLATFORM_DTB_PATH)

$(BOARD_BOOTIMG): BOARD_BOOTIMG:=$(BOARD_BOOTIMG)
$(BOARD_BOOTIMG): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)
$(BOARD_BOOTIMG): BOARD_ZIRCON_BOOTIMAGE2:=$(BOARD_ZIRCON_BOOTIMAGE2)
$(BOARD_BOOTIMG): BOARD_ZIRCON_BOOTIMAGE3:=$(BOARD_ZIRCON_BOOTIMAGE3)
$(BOARD_BOOTIMG): PLATFORM_KERNEL_OFFSET:=$(PLATFORM_KERNEL_OFFSET)
$(BOARD_BOOTIMG): PLATFORM_MEMBASE:=$(PLATFORM_MEMBASE)
$(BOARD_BOOTIMG): PLATFORM_CMDLINE:=$(PLATFORM_CMDLINE)
$(BOARD_BOOTIMG): MKBOOTFS_ARGS:=$(MKBOOTFS_ARGS)
$(BOARD_BOOTIMG): PLATFORM_DTB_PATH:=$(PLATFORM_DTB_PATH)

ifeq ($(PLATFORM_USE_GZIP),true)
$(BOARD_ZIRCON_BOOTIMAGE2): $(BOARD_ZIRCON_BOOTIMAGE)
	$(call BUILDECHO,generating $@)
	$(NOECHO)gzip -c $< > $@
endif

ifeq ($(PLATFORM_DTB_TYPE),append)
# device tree binary is appended to end of zircon image
$(BOARD_ZIRCON_BOOTIMAGE3): $(BOARD_ZIRCON_BOOTIMAGE2) $(PLATFORM_DTB_PATH)
	$(call BUILDECHO,generating $@)
	$(NOECHO)cat $(BOARD_ZIRCON_BOOTIMAGE2) $(PLATFORM_DTB_PATH) > $@
else ifeq ($(PLATFORM_DTB_TYPE),kdtb)
# device tree binary is appended with the mkkdtb tool
$(BOARD_ZIRCON_BOOTIMAGE3): $(BOARD_ZIRCON_BOOTIMAGE2) $(PLATFORM_DTB_PATH) $(KDTBTOOL)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(KDTBTOOL) $(BOARD_ZIRCON_BOOTIMAGE2) $(PLATFORM_DTB_PATH) $@
endif

$(BOARD_BOOTIMG): $(BOARD_ZIRCON_BOOTIMAGE3)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(MKBOOTIMG) --kernel $< --kernel_offset $(PLATFORM_KERNEL_OFFSET) $(MKBOOTFS_ARGS) \
	    --base $(PLATFORM_MEMBASE) --tags_offset 0xE000000 --cmdline $(PLATFORM_CMDLINE) -o $@

GENERATED += $(BOARD_ZIRCON_BOOTIMAGE2) $(BOARD_ZIRCON_BOOTIMAGE3) $(BOARD_BOOTIMG)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON_BOOTIMAGE2) $(BOARD_ZIRCON_BOOTIMAGE3) $(BOARD_BOOTIMG)

endif # PLATFORM_USE_MKBOOTIMG

# Android Verified Boot (AVB) support: Hash footer is added to the boot image
# and a vbmeta image is generated with signature for the boot image.
ifeq ($(PLATFORM_USE_AVB),true)

ifneq ($(PLATFORM_USE_MKBOOTIMG),true)
$(error PLATFORM_USE_AVB requires PLATFORM_USE_MKBOOTIMG)
endif
ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif
ifeq ($(PLATFORM_BOOT_PARTITION_SIZE),)
$(error PLATFORM_BOOT_PARTITION_SIZE not defined)
endif

AVBDIR := third_party/tools/android/avb
AVBTOOL := $(AVBDIR)/avbtool
AVB_KEY := $(AVBDIR)/test/data/testkey_atx_psk.pem
AVB_PUBLIC_KEY_METADATA := $(AVBDIR)/test/data/atx_metadata.bin

BOARD_VBMETA := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-vbmeta.img

$(BOARD_VBMETA): BOARD_VBMETA:=$(BOARD_VBMETA)
$(BOARD_VBMETA): BOARD_BOOTIMG:=$(BOARD_BOOTIMG)
$(BOARD_VBMETA): PLATFORM_BOOT_PARTITION_SIZE:=$(PLATFORM_BOOT_PARTITION_SIZE)

$(BOARD_VBMETA): $(BOARD_BOOTIMG)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(AVBTOOL) add_hash_footer --image $(BOARD_BOOTIMG) \
	    --partition_size $(PLATFORM_BOOT_PARTITION_SIZE) --partition_name boot
	$(NOECHO)$(AVBTOOL) make_vbmeta_image --include_descriptors_from_image $(BOARD_BOOTIMG) \
	    --algorithm SHA512_RSA4096 --key $(AVB_KEY) --public_key_metadata $(AVB_PUBLIC_KEY_METADATA) \
	    --padding_size 4096 --output $@

GENERATED += $(BOARD_VBMETA)
EXTRA_BUILDDEPS += $(BOARD_VBMETA)

endif # PLATFORM_USE_AVB

# clear variables that were passed in to us
PLATFORM_USE_SHIM :=
PLATFORM_USE_GZIP :=
PLATFORM_DTB_PATH :=
PLATFORM_DTB_TYPE :=
PLATFORM_USE_MKBOOTIMG :=
PLATFORM_USE_AVB :=
PLATFORM_BOARD_NAME :=
PLATFORM_KERNEL_OFFSET :=
PLATFORM_DTB_OFFSET :=
PLATFORM_MEMBASE :=
PLATFORM_CMDLINE :=
PLATFORM_BOOT_PARTITION_SIZE :=

# clear variables we set here
BOARD_ZIRCON_BOOTIMAGE :=
BOARD_ZIRCON_BOOTIMAGE2 :=
BOARD_ZIRCON_BOOTIMAGE3 :=
BOARD_BOOTIMG :=
BOARD_VBMETA :=
MKBOOTFS_ARGS :=
