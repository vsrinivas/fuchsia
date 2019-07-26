// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef void hexdump_printf_func_t(void* arg, const char* fmt, ...) __PRINTFLIKE(2, 3);

// A wrapper on fprintf that is passable as |hexdump_printf_func_t|.
void hexdump_stdio_printf(void* arg, const char* fmt, ...) __PRINTFLIKE(2, 3);

// Do a hex dump against stdout 32bits and 8bits at a time.
// The "very" in the name follows the kernel naming scheme.
// Output is printed like fprintf:
//   printf_func(printf_arg, "format string", ...);
// E.g., To print to stdout:
//   hexdump_very_ex(ptr, len, disp_addr, hexdump_stdio_printf, stdout);
void hexdump_very_ex(const void* ptr, size_t len, uint64_t disp_addr,
                     hexdump_printf_func_t* printf_func, void* printf_arg);
void hexdump8_very_ex(const void* ptr, size_t len, uint64_t disp_addr,
                      hexdump_printf_func_t* printf_func, void* printf_arg);

// Same as the "very" versions but output goes to stdout.
void hexdump_ex(const void* ptr, size_t len, uint64_t disp_addr);
void hexdump8_ex(const void* ptr, size_t len, uint64_t disp_addr);

static inline void hexdump(const void* ptr, size_t len) {
  hexdump_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}

static inline void hexdump8(const void* ptr, size_t len) {
  hexdump8_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}

__END_CDECLS
