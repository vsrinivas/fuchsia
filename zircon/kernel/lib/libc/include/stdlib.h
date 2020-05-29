// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#ifdef _KERNEL
#include <lib/heap.h>  //  lib/heap provides malloc/free definitions.
#endif

__BEGIN_CDECLS

long strtol(const char *nptr, char **endptr, int base);
unsigned long int strtoul(const char *nptr, char **endptr, int base);

void abort(void) __attribute__((noreturn));

#define RAND_MAX (0x7fffffff)

int rand(void);
void srand(unsigned int seed);

// Note: POSIX.1 specifies unsigned int, but we use uintptr_t instead.
int rand_r(uintptr_t *seed);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_
