#!/bin/sh
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

if [[ ! -f src/proc/lib/linux_uapi/wrapper.h ]]; then
  echo 'Please run this script from the root of your Fuchsia source tree.'
  exit 1
fi

readonly RAW_LINES="// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::x86_64_types::*;"

bindgen \
  --no-layout-tests \
  --size_t-is-usize \
  --ctypes-prefix="crate::x86_64_types" \
  --raw-line "${RAW_LINES}" \
  -o src/proc/lib/linux_uapi/src/x86_64.rs \
  src/proc/lib/linux_uapi/wrapper.h \
  -- \
  -I third_party/android/platform/bionic/libc/kernel/uapi \
  -I third_party/android/platform/bionic/libc/kernel/android/uapi \
  -I src/proc/lib/linux_uapi/stub
