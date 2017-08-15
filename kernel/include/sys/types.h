// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <limits.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef int status_t;

typedef uintptr_t addr_t;
typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

typedef uint64_t lk_time_t; // nanoseconds
#define INFINITE_TIME UINT64_MAX
#define LK_USEC(n) ((lk_time_t)(1000ULL * (n)))
#define LK_MSEC(n) ((lk_time_t)(1000000ULL * (n)))
#define LK_SEC(n)  ((lk_time_t)(1000000000ULL * (n)))

enum handler_return {
    INT_NO_RESCHEDULE = 0,
    INT_RESCHEDULE,
};

typedef intptr_t ssize_t;
#define SSIZE_MAX INTPTR_MAX

#define KB (1024UL)
#define MB (1024UL * KB)
#define GB (1024UL * MB)

