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

MODULE_SRCS += \
    $(LOCAL_DIR)/test-utils.c

MODULE_SO_NAME := test-utils

# launchpad, elfload, mxio are static so that every unittest doesn't have to
# mention them as a dependency as well.
# N.B. The order is important. Think ordering of args to the linker.
MODULE_STATIC_LIBS := \
    ulib/unittest \
    ulib/launchpad \
    ulib/elfload \
    ulib/mxio \
    ulib/runtime
MODULE_LIBS := ulib/magenta ulib/musl

include make/module.mk
