# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULES += \
    $(LOCAL_DIR)/controller \
    $(LOCAL_DIR)/codecs/qemu \
    $(LOCAL_DIR)/codecs/realtek \
