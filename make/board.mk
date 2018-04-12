# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif

BOARD_COMBO_BOOTDATA := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-combo-bootdata.bin

ifeq ($(PLATFORM_USE_SHIM),true)
ifeq ($(TARGET),arm64)
include kernel/target/arm64/boot-shim/rules.mk
else
$(error PLATFORM_USE_SHIM not supported for target $(TARGET))
endif
BOARD_BOOT_SHIM_OPTS := --header $(BOOT_SHIM_BIN) --header-align $(KERNEL_ALIGN)
else
BOARD_BOOT_SHIM_OPTS :=
endif

# capture board specific variables for the build rules
$(BOARD_COMBO_BOOTDATA): BOARD_COMBO_BOOTDATA:=$(BOARD_COMBO_BOOTDATA)
$(BOARD_COMBO_BOOTDATA): BOARD_BOOT_SHIM_OPTS:=$(BOARD_BOOT_SHIM_OPTS)

# combo bootdata package (kernel + bootdata)
$(BOARD_COMBO_BOOTDATA): $(MKBOOTFS) $(OUTLKBIN) $(USER_BOOTDATA) $(BOOT_SHIM_BIN)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $@ $(OUTLKBIN) $(USER_BOOTDATA) $(BOARD_BOOT_SHIM_OPTS)

GENERATED += $(BOARD_COMBO_BOOTDATA)
EXTRA_BUILDDEPS += $(BOARD_COMBO_BOOTDATA)

# clear variables that were passed in to us
PLATFORM_BOARD_NAME :=
PLATFORM_USE_SHIM :=

# clear variables we set here
BOARD_COMBO_BOOTDATA :=
BOARD_BOOT_SHIM_OPTS :=
