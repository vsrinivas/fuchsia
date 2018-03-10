# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/init.cpp

include make/module.mk

# copy our all-boards.list for the fuchsia build
# this file should contain only boards that we want to support outside of zircon
BOARD_LIST_SRC := $(LOCAL_DIR)/all-boards.list
BOARD_LIST_DEST := $(BUILDDIR)/export/all-boards.list

$(BOARD_LIST_DEST): $(BOARD_LIST_SRC)
	$(call BUILDECHO,copying $@)
	@$(MKDIR)
	$(NOECHO)cp $< $@

packages: $(BOARD_LIST_DEST)
