// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_
#define BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_

#include <stdint.h>

#define BACKTRACE_REQUEST_MAGIC ((uint64_t)0xee726573756d65ee)

// Invoke this function to cause crashlogger to print a backtrace
// and resume the thread without killing the process.

static inline void backtrace_request(void) {
#ifdef __x86_64__
    __asm__ ("int3" : : "a" (BACKTRACE_REQUEST_MAGIC));
#endif
#ifdef __aarch64__
    // This is what gdb uses.
    __asm__ ("mov x0, %0\n"
             "\tbrk 0"
             : : "r" (BACKTRACE_REQUEST_MAGIC) : "x0");
#endif
}

#endif // BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_
