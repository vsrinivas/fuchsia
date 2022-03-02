#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [[ ! -f src/proc/lib/linux_uapi/wrapper.h ]]; then
  echo 'Please run this script from the root of your Fuchsia source tree.'
  exit 1
fi

readonly RAW_LINES="// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{AsBytes, FromBytes};

pub use crate::x86_64_types::*;"

PATH="$PWD/prebuilt/third_party/rust/linux-x64/bin:$PATH" \
./prebuilt/third_party/rust_bindgen/linux-x64/bindgen \
  --no-layout-tests \
  --size_t-is-usize \
  --ignore-functions \
  --with-derive-default \
  --explicit-padding \
  --opaque-type=__sighandler_t \
  --opaque-type=__sigrestore_t \
  --ctypes-prefix="crate::x86_64_types" \
  --raw-line "${RAW_LINES}" \
  -o src/proc/lib/linux_uapi/src/x86_64.rs \
  src/proc/lib/linux_uapi/wrapper.h \
  -- \
  -target x86_64-pc-linux-gnu \
  -I third_party/android/platform/bionic/libc/kernel/uapi \
  -I third_party/android/platform/bionic/libc/kernel/uapi/asm-x86 \
  -I third_party/android/platform/bionic/libc/kernel/android/uapi \
  -I src/proc/lib/linux_uapi/stub \
  -nostdlibinc

# TODO(https://github.com/rust-lang/rust-bindgen/issues/2170): Remove in favor of bindgen support
# for custom derives.
sed -i \
  's/derive(Debug, Default, Copy, Clone)/derive(Debug, Default, Copy, Clone, AsBytes, FromBytes)/' \
  src/proc/lib/linux_uapi/src/x86_64.rs

# If the first line matches the expected derive expression, consume another line and do a search
# and replace for the expected identifier, rewriting the derive expression to include `FromBytes`.
# TODO(https://github.com/rust-lang/rust-bindgen/issues/2170): Remove in favor of bindgen support
# for custom derives.
sed -i \
  '/#\[derive(Copy, Clone)\]/ { N; s/.*\n\(pub \(struct\|union\) binder_transaction_data\)/#[derive(Copy, Clone, FromBytes)]\n\1/; p; d; }' \
  src/proc/lib/linux_uapi/src/x86_64.rs
