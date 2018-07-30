// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_CRASHLOGGER_H_
#define ZIRCON_CRASHLOGGER_H_

#define ZX_CRASHLOGGER_REQUEST_SELF_BT_MAGIC ((uint64_t)0xee726573756d65ee)

// Invoke this function to cause crashlogger to print a backtrace
// and resume the thread without killing the process.

static inline void zx_crashlogger_request_backtrace(void) {
#ifdef __x86_64__
    __asm__ ("int3" : : "a" (ZX_CRASHLOGGER_REQUEST_SELF_BT_MAGIC));
#endif
#ifdef __aarch64__
    // This is what gdb uses.
    __asm__ ("mov x0, %0\n"
             "\tbrk 0"
             : : "r" (ZX_CRASHLOGGER_REQUEST_SELF_BT_MAGIC) : "x0");
#endif
}

#endif // ZIRCON_CRASHLOGGER_H_
