// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __SYS_TYPES_H
#define __SYS_TYPES_H

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

typedef long long     off_t;

typedef int status_t;

typedef uintptr_t addr_t;
typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;

typedef uint64_t lk_time_t; // nanoseconds
#define INFINITE_TIME UINT64_MAX
#define LK_USEC(n) ((lk_time_t)(1000ULL * (n)))
#define LK_MSEC(n) ((lk_time_t)(1000000ULL * (n)))
#define LK_SEC(n)  ((lk_time_t)(1000000000ULL * (n)))

#define TIME_GTE(a, b) ((int64_t)((a) - (b)) >= 0)
#define TIME_LTE(a, b) ((int64_t)((a) - (b)) <= 0)
#define TIME_GT(a, b) ((int64_t)((a) - (b)) > 0)
#define TIME_LT(a, b) ((int64_t)((a) - (b)) < 0)

enum handler_return {
    INT_NO_RESCHEDULE = 0,
    INT_RESCHEDULE,
};

typedef intptr_t ssize_t;
#define SSIZE_MAX INTPTR_MAX

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#define KB (1024UL)
#define MB (1024UL * KB)
#define GB (1024UL * MB)

#endif
