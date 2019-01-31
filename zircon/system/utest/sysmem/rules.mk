# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# We'd rather the sysmem test be in utest like other tests, but it's convenient
# for the test to be included in builds that aren't zircon-only builds, and the
# inclusion of "test" group in such builds seems to not happen currently despite
# a comment to that effect in build/images/BUILD.gn, so for now we build this
# as a userapp in misc group, which does get included in non-zircon-only builds
# under /system/bin.
MODULE_TYPE := usertest
MODULE_USERTEST_GROUP := sys
#MODULE_TYPE := userapp
#MODULE_GROUP := misc

MODULE_SRCS += \
    $(LOCAL_DIR)/main.c \
    $(LOCAL_DIR)/sysmem_tests.cpp \

MODULE_NAME := sysmem-test

MODULE_LIBS := system/ulib/fdio system/ulib/c system/ulib/zircon system/ulib/unittest

MODULE_FIDL_LIBS := system/fidl/fuchsia-sysmem

MODULE_STATIC_LIBS := \
    system/ulib/fidl \
    system/ulib/fidl-async-2 \
    system/ulib/zx \
    system/ulib/zxcpp \

include make/module.mk
