# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build Zircon x86-64 userspace only.

ARCH := x86
SUBARCH := x86-64

TARGET := user
PLATFORM := user

include kernel/project/virtual/user.mk
