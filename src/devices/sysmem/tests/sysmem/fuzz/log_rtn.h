// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_LOG_RTN_H_
#define SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_LOG_RTN_H_

#define DBGRTN 0

#define LOGRTN(status, ...)           \
  {                                   \
    if (status != ZX_OK) {            \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }

#define LOGRTNC(condition, ...)       \
  {                                   \
    if ((condition)) {                \
      if (DBGRTN) {                   \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
      }                               \
      return 0;                       \
    }                                 \
  }

#endif  // SRC_DEVICES_SYSMEM_TESTS_SYSMEM_FUZZ_LOG_RTN_H_
