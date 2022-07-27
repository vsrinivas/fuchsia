// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MACROS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MACROS_H_

#include <lib/ddk/debug.h>

#define WAIT_ON(COND, N, UNITS)                            \
  ({                                                       \
    size_t count = 0;                                      \
    while (!(COND) && ++count <= static_cast<size_t>(N))   \
      zx_nanosleep(zx_deadline_after(ZX_##UNITS##SEC(1))); \
    count <= static_cast<size_t>(N);                       \
  })

#define WAIT_ON_US(COND, N) WAIT_ON(COND, N, U)
#define WAIT_ON_MS(COND, N) WAIT_ON(COND, N, M)

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_MACROS_H_
