# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS += \
	$(LOCAL_DIR)/pio.cpp

MODULE_LIBS := \
    system/dev/lib/mmio \
    system/ulib/fbl \
    system/ulib/hwreg \
    system/ulib/ddk \
    system/ulib/ddktl \
    system/ulib/zx \

include make/module.mk
