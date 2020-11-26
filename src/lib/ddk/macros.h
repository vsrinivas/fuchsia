// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_MACROS_H_
#define SRC_LIB_DDK_INCLUDE_DDK_MACROS_H_

#define DDK_ROUNDUP(a, b)       \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    ((_a + _b - 1) / _b * _b);  \
  })
#define DDK_ROUNDDOWN(a, b)     \
  ({                            \
    const __typeof(a) _a = (a); \
    const __typeof(b) _b = (b); \
    _a - (_a % _b);             \
  })

#endif  // SRC_LIB_DDK_INCLUDE_DDK_MACROS_H_
