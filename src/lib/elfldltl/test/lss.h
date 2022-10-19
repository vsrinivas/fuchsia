// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_TEST_LSS_H_
#define SRC_LIB_ELFLDLTL_TEST_LSS_H_

// This must be declared before the LSS header is included.
[[gnu::visibility("hidden")]] inline int gSyscallErrno;

#define SYS_ERRNO gSyscallErrno
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"

#include "linux_syscall_support.h"

#pragma GCC diagnostic pop
#undef SYS_ERRNO

#endif  // SRC_LIB_ELFLDLTL_TEST_LSS_H_
