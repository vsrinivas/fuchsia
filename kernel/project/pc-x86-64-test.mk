# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# top level project rules for the pc-x86-64-test project
#
SUBARCH := x86-64
MODULES +=

include project/virtual/nouser.mk
include project/virtual/test.mk
include project/target/pc-x86.mk
