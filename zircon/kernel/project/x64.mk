# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Project file to build zircon + user space on top of qemu
# emulating a standard PC with a 64bit x86 core

ARCH := x86
TARGET := pc

include kernel/project/virtual/test.mk
include kernel/project/virtual/user.mk
