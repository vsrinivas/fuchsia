// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_INCLUDE_ASSERT_H_
#define SRC_FIRMWARE_GIGABOOT_INCLUDE_ASSERT_H_

#include <stdio.h>

#define static_assert(e, msg) _Static_assert(e, msg)

#define assert(x)                                                       \
  do {                                                                  \
    if (unlikely(!(x))) {                                               \
      printf("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
      while (1) {                                                       \
      }                                                                 \
    }                                                                   \
  } while (0)

#endif  // SRC_FIRMWARE_GIGABOOT_INCLUDE_ASSERT_H_
