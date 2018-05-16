# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

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

# clear variables that were passed in to us
PLATFORM_BOARD_NAME :=
PLATFORM_USE_SHIM :=

# clear variables we set here
BOARD_ZIRCON_BOOTIMAGE :=
BOARD_BOOT_SHIM_OPTS :=

endif # PLATFORM_USE_SHIM
