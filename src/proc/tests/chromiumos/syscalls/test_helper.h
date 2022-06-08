// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_TEST_HELPER_H_
#define SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_TEST_HELPER_H_

#define SAFE_SYSCALL(X)                                                                         \
  ({                                                                                            \
    int retval;                                                                                 \
    retval = (X);                                                                               \
    if (retval < 0) {                                                                           \
      fprintf(stderr, "Error at %s:%d: %s (%d)\n", __FILE__, __LINE__, strerror(errno), errno); \
      exit(retval);                                                                             \
    };                                                                                          \
    retval;                                                                                     \
  })

#endif  // SRC_PROC_TESTS_CHROMIUMOS_SYSCALLS_TEST_HELPER_H_
