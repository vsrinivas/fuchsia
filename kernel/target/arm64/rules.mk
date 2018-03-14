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

GENERATED += $(OUT_ZIRCON_ZIMAGE)
EXTRA_BUILDDEPS += $(OUT_ZIRCON_ZIMAGE)

# include rules for our various arm64 boards
include $(LOCAL_DIR)/*/rules.mk
