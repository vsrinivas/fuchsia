# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# GN's target_cpu uses "x64", so it keeps the build integration
# simpler to have a project (and thus build directory) named that way.
include kernel/project/x86.mk
