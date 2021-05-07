#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script exists to manually generate rust bindings for zstd using bindgen.
# It is designed to run against Fuchsia's vendored copy of zstd and output bindings
# that zstd-safe can work with.

set -e

readonly COPYRIGHT="// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file."

# Run bindgen with flags set to match zstd-safe expectations.
bindgen wrapper.h \
    --blacklist-type=max_align_t \
    --size_t-is-usize \
    --rustified-enum=.* \
    --use-core \
    --ctypes-prefix libc \
    --raw-line "${COPYRIGHT}" \
    -o src/lib.rs \
    -- -I $FUCHSIA_DIR/third_party/zstd//src/lib