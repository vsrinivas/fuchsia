# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# all drivers have a so name so they get installed

ifeq ($(MODULE_SO_NAME),)
MODULE_SO_NAME := $(MODULE_NAME)
endif

# by default drivers tests live in driver/test/...

ifeq ($(MODULE_SO_INSTALL_NAME),)
MODULE_SO_INSTALL_NAME := driver/test/$(MODULE_SO_NAME).so
endif

include make/module-userlib.mk
