# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

PLATFORM := generic-arm

# Some boards need gzipped kernel image
OUT_ZIRCON_ZIMAGE := $(BUILDDIR)/z$(LKNAME).bin

$(OUT_ZIRCON_ZIMAGE): $(OUTLKBIN)
	$(call BUILDECHO,gzipping image $@)
	$(NOECHO)gzip -c $< > $@

GENERATED += $(OUT_ZIRCON_ZIMAGE)
EXTRA_BUILDDEPS += $(OUT_ZIRCON_ZIMAGE)

# copy our boards.list for the fuchsia build
# boards.list will only include boards capable of booting more than zircon
ARM64_BOARD_LIST := $(LOCAL_DIR)/boards.list
ZIRCON_BOARD_LIST := $(BUILDDIR)/export/boards.list

$(ZIRCON_BOARD_LIST): $(ARM64_BOARD_LIST)
	$(call BUILDECHO,copying $@)
	@$(MKDIR)
	$(NOECHO)cp $< $@

packages: $(ZIRCON_BOARD_LIST)

GENERATED += $(OUT_ZIRCON_ZIMAGE) $(ZIRCON_BOARD_LIST)
EXTRA_BUILDDEPS += $(OUT_ZIRCON_ZIMAGE) $(ZIRCON_BOARD_LIST)

# include rules for our various arm64 boards
include $(LOCAL_DIR)/*/rules.mk
