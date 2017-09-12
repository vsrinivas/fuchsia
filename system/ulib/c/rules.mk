# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The libc source lives in third_party/ulib/musl

# This rules.mk exists to satisfy the requirements
# of the Zircon build's module system, allowing
# libc to be referred to as "system/ulib/c" throughout the
# build instead of the more confusing "system/ulib/musl".

include third_party/ulib/musl/musl-rules.mk
