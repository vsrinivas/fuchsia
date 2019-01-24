# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := driver

MODULE_SRCS := $(LOCAL_DIR)/e1000_80003es2lan.c \
               $(LOCAL_DIR)/e1000_82540.c       \
               $(LOCAL_DIR)/e1000_82541.c       \
               $(LOCAL_DIR)/e1000_82542.c       \
               $(LOCAL_DIR)/e1000_82543.c       \
               $(LOCAL_DIR)/e1000_82571.c       \
               $(LOCAL_DIR)/e1000_82575.c       \
               $(LOCAL_DIR)/e1000_api.c         \
               $(LOCAL_DIR)/e1000_i210.c        \
               $(LOCAL_DIR)/e1000_ich8lan.c     \
               $(LOCAL_DIR)/e1000_mac.c         \
               $(LOCAL_DIR)/e1000_manage.c      \
               $(LOCAL_DIR)/e1000_mbx.c         \
               $(LOCAL_DIR)/e1000_nvm.c         \
               $(LOCAL_DIR)/e1000_osdep.c       \
               $(LOCAL_DIR)/e1000_phy.c         \
               $(LOCAL_DIR)/e1000_vf.c          \
               $(LOCAL_DIR)/fuchsia.c           \

MODULE_STATIC_LIBS := system/ulib/ddk

MODULE_LIBS := system/ulib/driver system/ulib/zircon system/ulib/c

MODULE_BANJO_LIBS := \
    system/banjo/ddk-protocol-ethernet \
    system/banjo/ddk-protocol-pci \

include make/module.mk
