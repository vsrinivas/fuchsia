# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# tests only differ from apps by their install location

MODULE_HOSTAPP_BIN := $(BUILDDIR)/host_tests/$(MODULE_NAME)

include make/module-hostapp.mk
