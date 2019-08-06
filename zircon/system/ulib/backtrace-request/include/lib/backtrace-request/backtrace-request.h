// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_
#define BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_

#include <stdint.h>

// Special value we put in the first register to let the exception handler know
// that we are just requesting a backtrace and we should resume the thread.
#define BACKTRACE_REQUEST_MAGIC ((uint64_t)0xee726573756d65ee)

// Prints a backtrace, resuming the thread without killing the process.
__attribute__((always_inline)) static inline void backtrace_request(void) {
  // Two instructions: one that sets a software breakpoint ("int3" on x64,
  // "brk" on arm64) and one that writes the "magic" value in the first
  // register ("a" on x64, "x0" on arm64).
  //
  // We set a software breakpoint to trigger the exception handling in
  // crashsvc, which will print the debug info, including the backtrace.
  //
  // We write the "magic" value in the first register so that the exception
  // handler can check for it and resume the thread if present.
#ifdef __x86_64__
  __asm__("int3" : : "a"(BACKTRACE_REQUEST_MAGIC));
#endif
#ifdef __aarch64__
  // This is what gdb uses.
  __asm__(
      "mov x0, %0\n"
      "\tbrk 0"
      :
      : "r"(BACKTRACE_REQUEST_MAGIC)
      : "x0");
#endif
}

#endif  // BACKTRACE_REQUEST_BACKTRACE_REQUEST_H_
