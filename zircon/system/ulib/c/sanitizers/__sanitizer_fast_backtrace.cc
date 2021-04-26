// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/sanitizer.h>

#include "backtrace.h"

__EXPORT size_t __sanitizer_fast_backtrace(uintptr_t buffer[], size_t buffer_size) {
  cpp20::span<uintptr_t> pcs{buffer, buffer_size};
  size_t count = __libc_sanitizer::BacktraceByShadowCallStack(pcs);
  if (count == 0) {
    count = __libc_sanitizer::BacktraceByFramePointer(pcs);
  }
  return count;
}
