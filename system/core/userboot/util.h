// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <stdarg.h>
#include <zircon/types.h>

// printl() is printf-like, understanding %s %p %d %u %x %zu %zd %zx.
// No other formatting features are supported.
void __PRINTFLIKE(2, 3) printl(zx_handle_t log, const char* fmt, ...);
void vprintl(zx_handle_t log, const char* fmt, va_list ap);

// fail() combines printl() with process exit
_Noreturn void __PRINTFLIKE(2, 3) fail(zx_handle_t log, const char* fmt, ...);

#define check(log, status, msg...) \
    do { if (status != ZX_OK) fail(log, msg); } while (0)

#pragma GCC visibility pop
