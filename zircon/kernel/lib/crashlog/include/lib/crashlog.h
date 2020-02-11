// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_H_
#define ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_H_

#include <zircon/boot/crash-reason.h>

#if defined(__aarch64__)

#include <arch/arm64.h>

#elif defined(__x86_64__)

#include <arch/x86.h>

#endif

typedef struct {
  uintptr_t base_address;
  iframe_t* iframe;
} crashlog_t;

extern crashlog_t crashlog;

// Serialize the crashlog to string in `out' up to `len' characters. If `reason'
// is OOM, then a different preamble will be used, and the backtrace will not be
// included.
size_t crashlog_to_string(char* out, const size_t len, zircon_crash_reason_t reason);

#endif  // ZIRCON_KERNEL_LIB_CRASHLOG_INCLUDE_LIB_CRASHLOG_H_
