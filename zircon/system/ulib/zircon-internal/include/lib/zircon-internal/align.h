// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_ALIGN_H
#define SYSROOT_ZIRCON_ALIGN_H

#include <zircon/limits.h>

#define ZX_ROUNDUP(a, b)        \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    ((_a + _b - 1) / _b * _b);  \
  })
#define ZX_ROUNDDOWN(a, b)      \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    _a - (_a % _b);             \
  })
#define ZX_ALIGN(a, b) ZX_ROUNDUP(a, b)
#define ZX_IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b)) - 1)))

#define ZX_PAGE_ALIGN(x) ZX_ALIGN((x), ZX_PAGE_SIZE)
#define ZX_ROUNDUP_PAGE_SIZE(x) ZX_ROUNDUP((x), ZX_PAGE_SIZE)
#define ZX_IS_PAGE_ALIGNED(x) ZX_IS_ALIGNED((x), ZX_PAGE_SIZE)

#endif  // SYSROOT_ZIRCON_ALIGN_H
