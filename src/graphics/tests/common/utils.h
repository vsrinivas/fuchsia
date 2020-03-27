// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_TESTS_COMMON_UTILS_H_
#define SRC_GRAPHICS_TESTS_COMMON_UTILS_H_

#include <stdio.h>

#define RTN_MSG(err, ...)                          \
  {                                                \
    fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__);                  \
    fflush(stderr);                                \
    return err;                                    \
  }

#endif  // SRC_GRAPHICS_TESTS_COMMON_UTILS_H_
