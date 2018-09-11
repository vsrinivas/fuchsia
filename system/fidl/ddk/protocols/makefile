# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

.DEFAULT_GOAL := all

LOCAL_DIR := $(patsubst %/,%,$(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))))
BUILD_DIR := ../../../../build-arm64
BANJOC := $(BUILD_DIR)/tools/banjoc

CLANG_FORMAT := clang-format --style="{Language: Cpp, ColumnLimit: 100, UseTab: Never, IndentWidth: 4, IndentCaseLabels: false, AlignAfterOpenBracket: Align, SpacesBeforeTrailingComments: 1, BreakBeforeBraces: Attach, AccessModifierOffset: -4, DerivePointerAlignment: false, PointerAlignment: Left, AllowShortFunctionsOnASingleLine: Inline, AllowShortIfStatementsOnASingleLine: false, KeepEmptyLinesAtTheStartOfBlocks: true, AlignEscapedNewlinesLeft: false, ForEachMacros: ['list_for_every_entry','list_for_every_entry_safe'], AlwaysBreakTemplateDeclarations: true}"

define run-banjo =
$(BANJOC) --ddk-header $(BUILD_DIR)/ddk/protocols/$@.h $(patsubst %.banjo,--files %.banjo, $^) \
	--files $(LOCAL_DIR)/$@.banjo
$(BANJOC) --ddktl-header $(BUILD_DIR)/ddktl/protocols/$@.h $(patsubst %.banjo,--files %.banjo, $^) \
	--files $(LOCAL_DIR)/$@.banjo
$(CLANG_FORMAT) $(BUILD_DIR)/ddk/protocols/$@.h > $(BUILD_DIR)/ddk/protocols/$@.formatted.h
$(CLANG_FORMAT) $(BUILD_DIR)/ddktl/protocols/$@.h > $(BUILD_DIR)/ddktl/protocols/$@.formatted.h
$(CLANG_FORMAT) $(BUILD_DIR)/ddktl/protocols/$@-internal.h > $(BUILD_DIR)/ddktl/protocols/$@-internal.formatted.h
cp -f $(BUILD_DIR)/ddk/protocols/$@.formatted.h ../../../ulib/ddk/include/ddk/protocol/$@.h
cp -f $(BUILD_DIR)/ddktl/protocols/$@.formatted.h ../../../ulib/ddktl/include/ddktl/protocol/$@.h
cp -f $(BUILD_DIR)/ddktl/protocols/$@-internal.formatted.h ../../../ulib/ddktl/include/ddktl/protocol/$@-internal.h
endef

acpi:
	$(run-banjo)

amlogic-canvas:
	$(run-banjo)

bad-block:
	$(run-banjo)

clk:
	$(run-banjo)

gpio:
	$(run-banjo)

all: acpi amlogic-canvas bad-block clk gpio
