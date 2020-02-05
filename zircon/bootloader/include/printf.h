// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Copyright (c) 2008 Travis Geiselbrecht

// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_BOOTLOADER_INCLUDE_PRINTF_H_
#define ZIRCON_BOOTLOADER_INCLUDE_PRINTF_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <uchar.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

#if !DISABLE_DEBUG_OUTPUT
#define printf(x...) _printf(x)
#else
static inline int __PRINTFLIKE(1, 2) printf(const char *fmt, ...) { return 0; }
#endif

int _printf(const char *fmt, ...) __PRINTFLIKE(1, 2);
int sprintf(char *str, const char *fmt, ...) __PRINTFLIKE(2, 3);
int snprintf(char *str, size_t len, const char *fmt, ...) __PRINTFLIKE(3, 4);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t len, const char *fmt, va_list ap);

/* printf engine that parses the format string and generates output */

/* function pointer to pass the printf engine, called back during the formatting.
 * input is a string to output, length bytes to output (or null on string),
 * return code is number of characters that would have been written, or error code (if negative)
 */
typedef int (*_printf_engine_output_func)(const char *str, size_t len, void *state);

int _printf_engine(_printf_engine_output_func out, void *state, const char *fmt, va_list ap);

// Print a wide string to console
int puts16(char16_t *str);

__END_CDECLS

#endif  // ZIRCON_BOOTLOADER_INCLUDE_PRINTF_H_
