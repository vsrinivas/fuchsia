// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define CRASHLOGGER_RESUME_MAGIC 0xee726573756d65eeull

// Invoke this function to cause crashlogger to print a backtrace
// and resume the thread without killing the process.

static inline void crashlogger_request_backtrace(void) {
#ifdef __x86_64__
    __asm__ ("int3" : : "a" (CRASHLOGGER_RESUME_MAGIC));
#endif
#ifdef __aarch64__
    // This is what gdb uses.
    __asm__ ("mov x0, %0\n"
             "\tbrk 0"
             : : "r" (CRASHLOGGER_RESUME_MAGIC) : "x0");
#endif
}
