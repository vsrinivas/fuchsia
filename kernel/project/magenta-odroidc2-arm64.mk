# Copyright 2017 The Fuchsia Authors
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# main project for qemu-aarch64
MODULES +=

ENABLE_BUILD_LISTFILES:=true

include kernel/project/virtual/user.mk
include kernel/project/virtual/test.mk
include kernel/project/target/odroidc2-a53.mk

