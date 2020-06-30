// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_UTIL_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_UTIL_H_

#include <lib/zx/debuglog.h>
#include <stdarg.h>
#include <zircon/status.h>
#include <zircon/types.h>

// printl() is printf-like, understanding %s %p %d %u %x %zu %zd %zx.
// No other formatting features are supported.
[[gnu::format(printf, 2, 3)]] void printl(const zx::debuglog& log, const char* fmt, ...);
void vprintl(const zx::debuglog& log, const char* fmt, va_list ap);

// fail() combines printl() with process exit
[[noreturn, gnu::format(printf, 2, 3)]] void fail(const zx::debuglog& log, const char* fmt, ...);

#define check(log, status, fmt, ...)                                      \
  do {                                                                    \
    if (status != ZX_OK)                                                  \
      fail(log, "%s: " fmt, zx_status_get_string(status), ##__VA_ARGS__); \
  } while (0)

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_UTIL_H_
