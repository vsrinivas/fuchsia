// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_INCLUDE_STDLIB_H_
#define SRC_FIRMWARE_GIGABOOT_INCLUDE_STDLIB_H_

#include <stddef.h>

int atoi(const char* nptr);
long atol(const char* nptr);
long long atoll(const char* nptr);

// stdlib functions needed for LZ4 library.

// Memory allocation functions just call down into the EFI allocation methods,
// but our code should prefer to use the EFI allocation methods directly, so
// that in tests we can control allocation behavior if needed. Using malloc
// directly would instead go to the default stdlib implementation in host tests.
void* malloc(size_t size);
void* calloc(size_t num, size_t size);
void free(void* addr);

void* memmove(void* dest, const void* src, size_t count);

// Our EFI toolchain thinks it's a Windows compiler, which causes it to inject
// this internal stack verification routine on any function that attempts to
// put a large amount of data on the stack. We don't need it here, other than
// to define it so that the compiler can find it.
void __chkstk(void);

// Similarly, LZ4 sees our toolchain pretending to be Windows and tries to use
// the Windows-specific byte-swapping functions. Point them back to the actual
// functions.
#define _byteswap_ulong __builtin_bswap32
#define _byteswap_uint64 __builtin_bswap64

// stdlib functions needed for libavb.

void abort(void) __attribute__((__noreturn__));

#endif  // SRC_FIRMWARE_GIGABOOT_INCLUDE_STDLIB_H_
