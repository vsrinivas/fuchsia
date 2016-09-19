# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MODULES += \
    app/shell

# hard disable building of sysroot in a no userspace build
ENABLE_BUILD_SYSROOT := false

