# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# main project for rpi3-arm64

ENABLE_BUILD_LISTFILES:=true

include kernel/project/virtual/user.mk
include kernel/project/virtual/test.mk
include kernel/project/target/rpi3-a53.mk

