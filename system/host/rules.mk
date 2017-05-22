# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

HOSTAPPS := \
	$(LOCAL_DIR)/bootserver/rules.mk \
	$(LOCAL_DIR)/fidl/rules.mk \
	$(LOCAL_DIR)/loglistener/rules.mk \
	$(LOCAL_DIR)/mdi/rules.mk \
	$(LOCAL_DIR)/merkleroot/rules.mk \
	$(LOCAL_DIR)/mkbootfs/rules.mk \
	$(LOCAL_DIR)/mkfs-msdosfs/rules.mk \
	$(LOCAL_DIR)/netprotocol/rules.mk \
	$(LOCAL_DIR)/sysgen/rules.mk \

include $(HOSTAPPS)
