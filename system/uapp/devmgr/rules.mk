# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_NAME := devmgr

MODULE_TYPE := userapp

# grab sources for all built-in drivers
# someday these will become dynamically loadable modules
LOCAL_SAVEDIR := $(LOCAL_DIR)
DRIVER_SRCS :=
DRIVERS := $(patsubst %/rules.mk,%,$(wildcard system/udev/*/driver.mk))
DRIVERS += $(patsubst %/rules.mk,%,$(wildcard third_party/udev/*/driver.mk))
-include $(DRIVERS)
LOCAL_DIR := $(LOCAL_SAVEDIR)

MODULE_SRCS := \
    $(DRIVER_SRCS) \

MODULE_SRCS += \
    $(LOCAL_DIR)/devmgr.c \
    $(LOCAL_DIR)/remote.c \
    $(LOCAL_DIR)/dmctl.c \
    $(LOCAL_DIR)/api.c \
    $(LOCAL_DIR)/vfs.c \
    $(LOCAL_DIR)/vfs-device.c \
    $(LOCAL_DIR)/vfs-boot.c \
    $(LOCAL_DIR)/vfs-memory.c \
    $(LOCAL_DIR)/mxio.c \
    $(LOCAL_DIR)/main.c

MODULE_DEPS := \
    ulib/musl \
    ulib/ddk \
    ulib/mxio \
    ulib/gfx \
    ulib/font \
    ulib/runtime \
    ulib/mxu \
    ulib/magenta

include make/module.mk
