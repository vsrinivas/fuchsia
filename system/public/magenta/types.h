// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/errors.h>
#include <stdint.h>
#ifndef __cplusplus
#ifndef _KERNEL
// We don't want to include <stdatomic.h> from the kernel code because the
// kernel definitions of atomic operations are incompatible with those defined
// in <stdatomic.h>.
//
// A better solution would be to use <stdatomic.h> and C11 atomic operation
// even in the kernel, but that would require modifying all the code that uses
// the existing homegrown atomics.
#include <stdatomic.h>
#endif
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

#define MX_SIGNAL_NONE              ((mx_signals_t)0u)
#define MX_OBJECT_SIGNAL_ALL        ((mx_signals_t)0x00ffffffu)
#define MX_USER_SIGNAL_ALL          ((mx_signals_t)0xff000000u)

#define MX_OBJECT_SIGNAL_0          ((mx_signals_t)1u << 0)
#define MX_OBJECT_SIGNAL_1          ((mx_signals_t)1u << 1)
#define MX_OBJECT_SIGNAL_2          ((mx_signals_t)1u << 2)
#define MX_OBJECT_SIGNAL_3          ((mx_signals_t)1u << 3)
#define MX_OBJECT_SIGNAL_4          ((mx_signals_t)1u << 4)
#define MX_OBJECT_SIGNAL_5          ((mx_signals_t)1u << 5)
#define MX_OBJECT_SIGNAL_6          ((mx_signals_t)1u << 6)
#define MX_OBJECT_SIGNAL_7          ((mx_signals_t)1u << 7)
#define MX_OBJECT_SIGNAL_8          ((mx_signals_t)1u << 8)
#define MX_OBJECT_SIGNAL_9          ((mx_signals_t)1u << 9)
#define MX_OBJECT_SIGNAL_10         ((mx_signals_t)1u << 10)
#define MX_OBJECT_SIGNAL_11         ((mx_signals_t)1u << 11)
#define MX_OBJECT_SIGNAL_12         ((mx_signals_t)1u << 12)
#define MX_OBJECT_SIGNAL_13         ((mx_signals_t)1u << 13)
#define MX_OBJECT_SIGNAL_14         ((mx_signals_t)1u << 14)
#define MX_OBJECT_SIGNAL_15         ((mx_signals_t)1u << 15)
#define MX_OBJECT_SIGNAL_16         ((mx_signals_t)1u << 16)
#define MX_OBJECT_SIGNAL_17         ((mx_signals_t)1u << 17)
#define MX_OBJECT_SIGNAL_18         ((mx_signals_t)1u << 18)
#define MX_OBJECT_SIGNAL_19         ((mx_signals_t)1u << 19)
#define MX_OBJECT_SIGNAL_20         ((mx_signals_t)1u << 20)
#define MX_OBJECT_SIGNAL_21         ((mx_signals_t)1u << 21)
#define MX_OBJECT_SIGNAL_22         ((mx_signals_t)1u << 22)
#define MX_OBJECT_SIGNAL_23         ((mx_signals_t)1u << 23)

#define MX_USER_SIGNAL_0            ((mx_signals_t)1u << 24)
#define MX_USER_SIGNAL_1            ((mx_signals_t)1u << 25)
#define MX_USER_SIGNAL_2            ((mx_signals_t)1u << 26)
#define MX_USER_SIGNAL_3            ((mx_signals_t)1u << 27)
#define MX_USER_SIGNAL_4            ((mx_signals_t)1u << 28)
#define MX_USER_SIGNAL_5            ((mx_signals_t)1u << 29)
#define MX_USER_SIGNAL_6            ((mx_signals_t)1u << 30)
#define MX_USER_SIGNAL_7            ((mx_signals_t)1u << 31)

#define MX_SIGNAL_HANDLE_CLOSED     MX_OBJECT_SIGNAL_23

// Event
#define MX_EVENT_SIGNALED           MX_OBJECT_SIGNAL_3
#define MX_EVENT_SIGNAL_MASK        (MX_USER_SIGNAL_ALL | MX_OBJECT_SIGNAL_3)

// EventPair
#define MX_EPAIR_SIGNALED           MX_OBJECT_SIGNAL_3
#define MX_EPAIR_CLOSED             MX_OBJECT_SIGNAL_2
#define MX_EPAIR_SIGNAL_MASK        (MX_USER_SIGNAL_ALL | MX_OBJECT_SIGNAL_2 | MX_OBJECT_SIGNAL_3)

// Task signals (process, thread, job)
#define MX_TASK_TERMINATED          MX_OBJECT_SIGNAL_3
#define MX_TASK_SIGNAL_MASK         MX_OBJECT_SIGNAL_3

// Channel
#define MX_CHANNEL_READABLE         MX_OBJECT_SIGNAL_0
#define MX_CHANNEL_WRITABLE         MX_OBJECT_SIGNAL_1
#define MX_CHANNEL_PEER_CLOSED      MX_OBJECT_SIGNAL_2

// Socket
#define MX_SOCKET_READABLE          MX_OBJECT_SIGNAL_0
#define MX_SOCKET_WRITABLE          MX_OBJECT_SIGNAL_1
#define MX_SOCKET_PEER_CLOSED       MX_OBJECT_SIGNAL_2

// Data pipe
#define MX_DATAPIPE_READABLE        MX_OBJECT_SIGNAL_0
#define MX_DATAPIPE_WRITABLE        MX_OBJECT_SIGNAL_1
#define MX_DATAPIPE_PEER_CLOSED     MX_OBJECT_SIGNAL_2
#define MX_DATAPIPE_READ_THRESHOLD  MX_OBJECT_SIGNAL_4
#define MX_DATAPIPE_WRITE_THRESHOLD MX_OBJECT_SIGNAL_5

// Legacy signal names, to be removed.
#define MX_SIGNAL_READABLE          MX_OBJECT_SIGNAL_0
#define MX_SIGNAL_WRITABLE          MX_OBJECT_SIGNAL_1
#define MX_SIGNAL_PEER_CLOSED       MX_OBJECT_SIGNAL_2
#define MX_SIGNAL_SIGNALED          MX_OBJECT_SIGNAL_3

typedef struct {
    mx_handle_t handle;
    mx_signals_t waitfor;
    mx_signals_t pending;
} mx_wait_item_t;

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
#define MX_FLAG_REPLY_CHANNEL            (1u << 0)
#define MX_CHANNEL_CREATE_REPLY_CHANNEL  (1u << 0)

// clock ids
#define MX_CLOCK_MONOTONIC        (0u)

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// size
typedef uintptr_t mx_size_t;
typedef intptr_t mx_ssize_t;
#define MX_SIZE_MAX UINTPTR_MAX
#define MX_SSIZE_MAX INTPTR_MAX

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
#ifdef _KERNEL
typedef int mx_futex_t;
#else
typedef atomic_int mx_futex_t;
#endif
#endif

__END_CDECLS
