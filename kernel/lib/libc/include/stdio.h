// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <printf.h>
#include <sys/types.h>
#include <lib/io.h>

__BEGIN_CDECLS

int putchar(int c);

int puts(const char *str);

int getchar(void);

#if !DISABLE_DEBUG_OUTPUT
#define printf(x...) _printf(x)
#define vprintf(x...) _vprintf(x)
#else
static inline int __PRINTFLIKE(1, 2) printf(const char *fmt, ...) { return 0; }
static inline int vprintf(const char *fmt, va_list ap) { return 0; }
#endif

int _printf(const char *fmt, ...) __PRINTFLIKE(1, 2);
int _vprintf(const char *fmt, va_list ap);

int sprintf(char *str, const char *fmt, ...) __PRINTFLIKE(2, 3);
int snprintf(char *str, size_t len, const char *fmt, ...) __PRINTFLIKE(3, 4);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t len, const char *fmt, va_list ap);


__END_CDECLS
