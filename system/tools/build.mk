# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

SUBDIR_INCLUDES := \
    $(LOCAL_DIR)/bootserver/build.mk \
    $(LOCAL_DIR)/loglistener/build.mk \
    $(LOCAL_DIR)/mkbootfs/build.mk \
    $(LOCAL_DIR)/netprotocol/build.mk \
    $(LOCAL_DIR)/sysgen/build.mk \

include $(SUBDIR_INCLUDES)
