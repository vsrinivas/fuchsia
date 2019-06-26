// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_MACROS_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_MACROS_H_

#include <ddk/debug.h>

#define WAIT_ON(COND, N, UNITS)                            \
  ({                                                       \
    int count = 0;                                         \
    while (!(COND) && ++count <= N)                        \
      zx_nanosleep(zx_deadline_after(ZX_##UNITS##SEC(1))); \
    count <= N;                                            \
  })

#define WAIT_ON_US(COND, N) WAIT_ON(COND, N, U)
#define WAIT_ON_MS(COND, N) WAIT_ON(COND, N, M)

#define LOG_ERROR(fmt, ...) zxlogf(ERROR, "i915: " fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) zxlogf(WARN, "i915: " fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) zxlogf(INFO, "i915: " fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) zxlogf(TRACE, "i915: " fmt, ##__VA_ARGS__)
#define LOG_SPEW(fmt, ...) zxlogf(SPEW, "i915: " fmt, ##__VA_ARGS__)

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_MACROS_H_
