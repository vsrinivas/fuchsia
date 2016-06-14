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

MODULE_TYPE := userlib

MODULE_SRCS := \
    $(LOCAL_DIR)/mutex.c \
    $(LOCAL_DIR)/process.c \
    $(LOCAL_DIR)/strstatus.c \
    $(LOCAL_DIR)/thread.c \
    $(LOCAL_DIR)/tls.c \
    $(LOCAL_DIR)/sysinfo.c \

MODULE_DEPS += \
    ulib/magenta

# for stdint.h
MODULE_HEADER_DEPS += \
    ulib/musl

include make/module.mk
