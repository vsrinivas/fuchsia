# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Rules for building boot shim and prepending it to ZBI.
ifeq ($(PLATFORM_USE_SHIM),true)

ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif

BOARD_ZIRCON_BOOTIMAGE := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.bin

ifeq ($(TARGET),arm64)
include kernel/target/arm64/boot-shim/rules.mk
else
$(error PLATFORM_USE_SHIM not supported for target $(TARGET))
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
DUMMY_DTB := kernel/target/arm64/dtb/dummy-device-tree.dtb

BOARD_BOOTIMG := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-boot.img
BOARD_ZIRCON_ZBOOTIMAGE := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.gz
BOARD_ZIRCON_ZBOOTIMAGE_DTB := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon-bootimage.gz-dtb

$(BOARD_ZIRCON_ZBOOTIMAGE): BOARD_ZIRCON_ZBOOTIMAGE:=$(BOARD_ZIRCON_ZBOOTIMAGE)
$(BOARD_ZIRCON_ZBOOTIMAGE): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)

$(BOARD_ZIRCON_ZBOOTIMAGE_DTB): BOARD_ZIRCON_ZBOOTIMAGE_DTB:=$(BOARD_ZIRCON_ZBOOTIMAGE_DTB)
$(BOARD_ZIRCON_ZBOOTIMAGE_DTB): BOARD_ZIRCON_ZBOOTIMAGE:=$(BOARD_ZIRCON_ZBOOTIMAGE)

$(BOARD_BOOTIMG): BOARD_BOOTIMG:=$(BOARD_BOOTIMG)
$(BOARD_BOOTIMG): BOARD_ZIRCON_BOOTIMAGE:=$(BOARD_ZIRCON_BOOTIMAGE)
$(BOARD_BOOTIMG): BOARD_ZIRCON_ZBOOTIMAGE:=$(BOARD_ZIRCON_ZBOOTIMAGE)
$(BOARD_BOOTIMG): BOARD_ZIRCON_ZBOOTIMAGE_DTB:=$(BOARD_ZIRCON_ZBOOTIMAGE_DTB)
$(BOARD_BOOTIMG): PLATFORM_KERNEL_OFFSET:=$(PLATFORM_KERNEL_OFFSET)
$(BOARD_BOOTIMG): PLATFORM_MEMBASE:=$(PLATFORM_MEMBASE)
$(BOARD_BOOTIMG): PLATFORM_CMDLINE:=$(PLATFORM_CMDLINE)

$(BOARD_ZIRCON_ZBOOTIMAGE): $(BOARD_ZIRCON_BOOTIMAGE)
	$(call BUILDECHO,generating $@)
	$(NOECHO)gzip -c $< > $@

GENERATED += $(BOARD_ZIRCON_ZBOOTIMAGE)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON_ZBOOTIMAGE)

ifeq ($(PLATFORM_USE_MKKDTB),true)
KDTBTOOL=$(BUILDDIR)/tools/mkkdtb

$(BOARD_ZIRCON_ZBOOTIMAGE_DTB): $(BOARD_ZIRCON_ZBOOTIMAGE) $(DUMMY_DTB) $(KDTBTOOL)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(KDTBTOOL) $(BOARD_ZIRCON_ZBOOTIMAGE) $(DUMMY_DTB) $@
else
$(BOARD_ZIRCON_ZBOOTIMAGE_DTB): $(BOARD_ZIRCON_ZBOOTIMAGE) $(DUMMY_DTB)
	$(call BUILDECHO,generating $@)
	$(NOECHO)cat $(BOARD_ZIRCON_ZBOOTIMAGE) $(DUMMY_DTB) > $@
endif

GENERATED += $(BOARD_ZIRCON_ZBOOTIMAGE_DTB)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON_ZBOOTIMAGE_DTB)

$(BOARD_BOOTIMG): $(BOARD_ZIRCON_ZBOOTIMAGE_DTB)
	$(call BUILDECHO,generating $@)
	$(NOECHO)$(MKBOOTIMG) --kernel $< --kernel_offset $(PLATFORM_KERNEL_OFFSET) \
	    --base $(PLATFORM_MEMBASE) --tags_offset 0xE000000 --cmdline $(PLATFORM_CMDLINE) -o $@

GENERATED += $(BOARD_BOOTIMG)
EXTRA_BUILDDEPS += $(BOARD_BOOTIMG)

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
PLATFORM_USE_MKBOOTIMG :=
PLATFORM_USE_AVB :=
PLATFORM_BOARD_NAME :=
PLATFORM_KERNEL_OFFSET :=
PLATFORM_MEMBASE :=
PLATFORM_CMDLINE :=
PLATFORM_BOOT_PARTITION_SIZE :=

# clear variables we set here
BOARD_ZIRCON_BOOTIMAGE :=
BOARD_ZIRCON_ZBOOTIMAGE :=
BOARD_ZIRCON_ZBOOTIMAGE_DTB :=
BOARD_BOOTIMG :=
BOARD_VBMETA :=

