// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_SYS_TYPES_H_
#define ZIRCON_KERNEL_INCLUDE_SYS_TYPES_H_

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

typedef intptr_t ssize_t;
#define SSIZE_MAX INTPTR_MAX

#define KB (1024UL)
#define MB (1024UL * KB)
#define GB (1024UL * MB)

#endif  // ZIRCON_KERNEL_INCLUDE_SYS_TYPES_H_
