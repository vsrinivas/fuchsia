// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/errors.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdatomic.h>
#endif

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

typedef int32_t mx_handle_t;
#define MX_HANDLE_INVALID         ((mx_handle_t)0)

// Same as kernel status_t
typedef int32_t mx_status_t;

// time in nanoseconds
typedef uint64_t mx_time_t;
#define MX_TIME_INFINITE UINT64_MAX
#define MX_USEC(n) ((mx_time_t)(1000ULL * (n)))
#define MX_MSEC(n) ((mx_time_t)(1000000ULL * (n)))
#define MX_SEC(n)  ((mx_time_t)(1000000000ULL * (n)))

typedef uint32_t mx_signals_t;
#define MX_SIGNAL_NONE            ((mx_signals_t)0u)
#define MX_SIGNAL_READABLE        ((mx_signals_t)1u << 0)
#define MX_SIGNAL_WRITABLE        ((mx_signals_t)1u << 1)
#define MX_SIGNAL_PEER_CLOSED     ((mx_signals_t)1u << 2)

#define MX_SIGNAL_SIGNALED        MX_SIGNAL_SIGNAL0
#define MX_SIGNAL_SIGNAL0         ((mx_signals_t)1u << 3)
#define MX_SIGNAL_SIGNAL1         ((mx_signals_t)1u << 4)
#define MX_SIGNAL_SIGNAL2         ((mx_signals_t)1u << 5)
#define MX_SIGNAL_SIGNAL3         ((mx_signals_t)1u << 6)
#define MX_SIGNAL_SIGNAL4         ((mx_signals_t)1u << 7)
#define MX_SIGNAL_SIGNAL_ALL      ((mx_signals_t)31u << 3)

#define MX_SIGNAL_READ_THRESHOLD  ((mx_signals_t)1u << 8)
#define MX_SIGNAL_WRITE_THRESHOLD ((mx_signals_t)1u << 9)

typedef struct {
    mx_signals_t satisfied;
    mx_signals_t satisfiable;
} mx_signals_state_t;

typedef uint32_t mx_rights_t;
#define MX_RIGHT_NONE             ((mx_rights_t)0u)
#define MX_RIGHT_DUPLICATE        ((mx_rights_t)1u << 0)
#define MX_RIGHT_TRANSFER         ((mx_rights_t)1u << 1)
#define MX_RIGHT_READ             ((mx_rights_t)1u << 2)
#define MX_RIGHT_WRITE            ((mx_rights_t)1u << 3)
#define MX_RIGHT_EXECUTE          ((mx_rights_t)1u << 4)
#define MX_RIGHT_MAP              ((mx_rights_t)1u << 5)
#define MX_RIGHT_GET_PROPERTY     ((mx_rights_t)1u << 6)
#define MX_RIGHT_SET_PROPERTY     ((mx_rights_t)1u << 7)
#define MX_RIGHT_DEBUG            ((mx_rights_t)1u << 8)
#define MX_RIGHT_SAME_RIGHTS      ((mx_rights_t)1u << 31)

// flags to vm map routines
#define MX_VM_FLAG_FIXED          (1u << 0)
#define MX_VM_FLAG_PERM_READ      (1u << 1)
#define MX_VM_FLAG_PERM_WRITE     (1u << 2)
#define MX_VM_FLAG_PERM_EXECUTE   (1u << 3)
#define MX_VM_FLAG_ALLOC_BASE     (1u << 4)

// flags to channel routines
#define MX_FLAG_REPLY_PIPE               (1u << 0)
#define MX_CHANNEL_CREATE_REPLY_CHANNEL  (1u << 0)

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// size
typedef uintptr_t mx_size_t;
typedef intptr_t mx_ssize_t;

// offset
typedef uint64_t mx_off_t;
typedef int64_t mx_rel_off_t;

// Maximum string length for kernel names (process name, thread name, etc)
#define MX_MAX_NAME_LEN           (32)

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

// Flags which can be used to to control cache policy for APIs which map memory.
typedef enum {
    MX_CACHE_POLICY_CACHED          = 0,
    MX_CACHE_POLICY_UNCACHED        = 1,
    MX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    MX_CACHE_POLICY_WRITE_COMBINING = 3,
} mx_cache_policy_t;

#ifdef __cplusplus
// We cannot use <stdatomic.h> with C++ code as _Atomic qualifier defined by
// C11 is not valid in C++11. There is not a single standard name that can
// be used in both C and C++. C++ <atomic> defines names which are equivalent
// to those in <stdatomic.h>, but these are contained in the std namespace.
//
// In kernel, the only operation done is a user_copy (of sizeof(int)) inside a
// lock; otherwise the futex address is treated as a key.
typedef int mx_futex_t;
#else
typedef atomic_int mx_futex_t;
#endif

__END_CDECLS
