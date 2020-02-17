// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_
#define ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_

#include <malloc.h>
#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <arch/defines.h>

__BEGIN_CDECLS

long strtol(const char *nptr, char **endptr, int base);
unsigned long int strtoul(const char *nptr, char **endptr, int base);

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))

#define ALIGN(a, b) ROUNDUP(a, b)
#define IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b)) - 1)))

void abort(void) __attribute__((noreturn));

#define RAND_MAX (0x7fffffff)

int rand(void);
void srand(unsigned int seed);

// Note: POSIX.1 specifies unsigned int, but we use uint64_t instead.
int rand_r(uint64_t* seed);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_LIBC_INCLUDE_STDLIB_H_
