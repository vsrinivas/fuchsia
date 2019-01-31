# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

ifeq ($(MODULE_USERTEST_GROUP),)
    MODULE_USERTEST_GROUP := sys
endif

MODULE_INSTALL_PATH := test/$(MODULE_USERTEST_GROUP)

include make/module-userapp.mk

MODULE_USERTEST_GROUP :=
