# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

include make/module.mk

# Make available to host tools.
# Note: Technically this isn't necessary yet. It will be needed if/when
# any .c/.cpp files are added.
# Note: Host tools still have to add a line like the following to their
# rules.mk entry:
# MODULE_COMPILEFLAGS := -Isystem/ulib/hash/include

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

include make/module.mk
