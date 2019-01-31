# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# host blobfs lib

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR).hostlib

MODULE_TYPE := hostlib

MODULE_SRCS := \
    $(LOCAL_DIR)/common.cpp \
    $(LOCAL_DIR)/file_size_recorder.cpp \

MODULE_COMPILEFLAGS := \
    -Werror-implicit-function-declaration \
    -Wstrict-prototypes -Wwrite-strings \
    -Isystem/ulib/zircon/include \
    -Isystem/ulib/fbl/include \

MODULE_HOST_LIBS := \
    system/ulib/fbl.hostlib \

include make/module.mk
