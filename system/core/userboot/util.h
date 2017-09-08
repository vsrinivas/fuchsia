// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <stdarg.h>
#include <magenta/types.h>

// printl() is printf-like, understanding %s %p %d %u %x %zu %zd %zx.
// No other formatting features are supported.
void printl(mx_handle_t log, const char* fmt, ...);
void vprintl(mx_handle_t log, const char* fmt, va_list ap);

void print(mx_handle_t log, const char* s, ...) __attribute__((sentinel));
_Noreturn void fail(mx_handle_t log, mx_status_t status, const char* msg);

static inline void check(mx_handle_t log,
                         mx_status_t status, const char* msg) {
    if (status != MX_OK)
        fail(log, status, msg);
}

#pragma GCC visibility pop
