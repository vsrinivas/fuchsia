// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/errors.h>
#include <stdbool.h>
#include <stddef.h>
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

#ifdef _KERNEL
typedef uint32_t mx_handle_t;
#else
typedef int32_t mx_handle_t;
#endif

#define MX_HANDLE_INVALID         ((mx_handle_t)0)

// Same as kernel status_t
typedef int32_t mx_status_t;

// absolute time in nanoseconds (generally with respect to the monotonic clock)
typedef uint64_t mx_time_t;
// a duration in nanoseconds
typedef uint64_t mx_duration_t;
#define MX_TIME_INFINITE UINT64_MAX
#define MX_USEC(n) ((mx_duration_t)(1000ULL * (n)))
#define MX_MSEC(n) ((mx_duration_t)(1000000ULL * (n)))
#define MX_SEC(n)  ((mx_duration_t)(1000000000ULL * (n)))

typedef uint32_t mx_signals_t;

#define MX_SIGNAL_NONE              ((mx_signals_t)0u)
#define MX_USER_SIGNAL_ALL          ((mx_signals_t)0xff000000u)

// Implementation details (__MX_* not intended for public consumption)
//
// Signals that have a common meaning where used are named with that
// meaning.  Signals that do not, or are not yet in use, are named
// generically.
#define __MX_OBJECT_SIGNAL_ALL      ((mx_signals_t)0x00ffffffu)
#define __MX_OBJECT_READABLE        ((mx_signals_t)1u << 0)
#define __MX_OBJECT_WRITABLE        ((mx_signals_t)1u << 1)
#define __MX_OBJECT_PEER_CLOSED     ((mx_signals_t)1u << 2)
#define __MX_OBJECT_SIGNALED        ((mx_signals_t)1u << 3)
#define __MX_OBJECT_SIGNAL_4        ((mx_signals_t)1u << 4)
#define __MX_OBJECT_SIGNAL_5        ((mx_signals_t)1u << 5)
#define __MX_OBJECT_SIGNAL_6        ((mx_signals_t)1u << 6)
#define __MX_OBJECT_SIGNAL_7        ((mx_signals_t)1u << 7)
#define __MX_OBJECT_SIGNAL_8        ((mx_signals_t)1u << 8)
#define __MX_OBJECT_SIGNAL_9        ((mx_signals_t)1u << 9)
#define __MX_OBJECT_SIGNAL_10       ((mx_signals_t)1u << 10)
#define __MX_OBJECT_SIGNAL_11       ((mx_signals_t)1u << 11)
#define __MX_OBJECT_SIGNAL_12       ((mx_signals_t)1u << 12)
#define __MX_OBJECT_SIGNAL_13       ((mx_signals_t)1u << 13)
#define __MX_OBJECT_SIGNAL_14       ((mx_signals_t)1u << 14)
#define __MX_OBJECT_SIGNAL_15       ((mx_signals_t)1u << 15)
#define __MX_OBJECT_SIGNAL_16       ((mx_signals_t)1u << 16)
#define __MX_OBJECT_SIGNAL_17       ((mx_signals_t)1u << 17)
#define __MX_OBJECT_SIGNAL_18       ((mx_signals_t)1u << 18)
#define __MX_OBJECT_SIGNAL_19       ((mx_signals_t)1u << 19)
#define __MX_OBJECT_SIGNAL_20       ((mx_signals_t)1u << 20)
#define __MX_OBJECT_SIGNAL_21       ((mx_signals_t)1u << 21)
#define __MX_OBJECT_LAST_HANDLE     ((mx_signals_t)1u << 22)
#define __MX_OBJECT_HANDLE_CLOSED   ((mx_signals_t)1u << 23)



// User Signals (for mx_object_signal() and mx_object_signal_peer())
#define MX_USER_SIGNAL_0            ((mx_signals_t)1u << 24)
#define MX_USER_SIGNAL_1            ((mx_signals_t)1u << 25)
#define MX_USER_SIGNAL_2            ((mx_signals_t)1u << 26)
#define MX_USER_SIGNAL_3            ((mx_signals_t)1u << 27)
#define MX_USER_SIGNAL_4            ((mx_signals_t)1u << 28)
#define MX_USER_SIGNAL_5            ((mx_signals_t)1u << 29)
#define MX_USER_SIGNAL_6            ((mx_signals_t)1u << 30)
#define MX_USER_SIGNAL_7            ((mx_signals_t)1u << 31)

// Cancelation (handle was closed while waiting with it)
#define MX_SIGNAL_HANDLE_CLOSED     __MX_OBJECT_HANDLE_CLOSED

// Only one user-more reference (handle) to the object exists.
#define MX_SIGNAL_LAST_HANDLE       __MX_OBJECT_LAST_HANDLE

// Event
#define MX_EVENT_SIGNALED           __MX_OBJECT_SIGNALED
#define MX_EVENT_SIGNAL_MASK        (MX_USER_SIGNAL_ALL | __MX_OBJECT_SIGNALED)

// EventPair
#define MX_EPAIR_SIGNALED           __MX_OBJECT_SIGNALED
#define MX_EPAIR_PEER_CLOSED        __MX_OBJECT_PEER_CLOSED
#define MX_EPAIR_SIGNAL_MASK        (MX_USER_SIGNAL_ALL | __MX_OBJECT_SIGNALED | __MX_OBJECT_PEER_CLOSED)

// Channel
#define MX_CHANNEL_READABLE         __MX_OBJECT_READABLE
#define MX_CHANNEL_WRITABLE         __MX_OBJECT_WRITABLE
#define MX_CHANNEL_PEER_CLOSED      __MX_OBJECT_PEER_CLOSED

// Socket
#define MX_SOCKET_READABLE          __MX_OBJECT_READABLE
#define MX_SOCKET_WRITABLE          __MX_OBJECT_WRITABLE
#define MX_SOCKET_PEER_CLOSED       __MX_OBJECT_PEER_CLOSED
#define MX_SOCKET_READ_DISABLED     __MX_OBJECT_SIGNAL_4
#define MX_SOCKET_WRITE_DISABLED    __MX_OBJECT_SIGNAL_5
#define MX_SOCKET_CONTROL_READABLE  __MX_OBJECT_SIGNAL_6
#define MX_SOCKET_CONTROL_WRITABLE  __MX_OBJECT_SIGNAL_7

// Port
#define MX_PORT_READABLE            __MX_OBJECT_READABLE

// Fifo
#define MX_FIFO_READABLE            __MX_OBJECT_READABLE
#define MX_FIFO_WRITABLE            __MX_OBJECT_WRITABLE
#define MX_FIFO_PEER_CLOSED         __MX_OBJECT_PEER_CLOSED

// Task signals (process, thread, job)
#define MX_TASK_TERMINATED          __MX_OBJECT_SIGNALED

// Job
#define MX_JOB_NO_PROCESSES         __MX_OBJECT_SIGNALED
#define MX_JOB_NO_JOBS              __MX_OBJECT_SIGNAL_4

// Process
#define MX_PROCESS_TERMINATED       __MX_OBJECT_SIGNALED

// Thread
#define MX_THREAD_TERMINATED        __MX_OBJECT_SIGNALED

// Log
#define MX_LOG_READABLE             __MX_OBJECT_READABLE
#define MX_LOG_WRITABLE             __MX_OBJECT_WRITABLE

// Timer
#define MX_TIMER_SIGNALED           __MX_OBJECT_SIGNALED

// global kernel object id.
typedef uint64_t mx_koid_t;
#define MX_KOID_INVALID ((uint64_t) 0)
#define MX_KOID_KERNEL  ((uint64_t) 1)

// Transaction ID and argument types for mx_channel_call.
typedef uint32_t mx_txid_t;

typedef struct {
    const void* wr_bytes;
    const mx_handle_t* wr_handles;
    void *rd_bytes;
    mx_handle_t* rd_handles;
    uint32_t wr_num_bytes;
    uint32_t wr_num_handles;
    uint32_t rd_num_bytes;
    uint32_t rd_num_handles;
} mx_channel_call_args_t;

// Structure for mx_object_wait_many():
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
#define MX_RIGHT_ENUMERATE        ((mx_rights_t)1u << 8)
#define MX_RIGHT_DESTROY          ((mx_rights_t)1u << 9)
#define MX_RIGHT_SET_POLICY       ((mx_rights_t)1u << 10)
#define MX_RIGHT_GET_POLICY       ((mx_rights_t)1u << 11)
#define MX_RIGHT_SIGNAL           ((mx_rights_t)1u << 12)
#define MX_RIGHT_SIGNAL_PEER      ((mx_rights_t)1u << 13)

#define MX_RIGHT_SAME_RIGHTS      ((mx_rights_t)1u << 31)

// VM Object opcodes
#define MX_VMO_OP_COMMIT                 1u
#define MX_VMO_OP_DECOMMIT               2u
#define MX_VMO_OP_LOCK                   3u
#define MX_VMO_OP_UNLOCK                 4u
#define MX_VMO_OP_LOOKUP                 5u
#define MX_VMO_OP_CACHE_SYNC             6u
#define MX_VMO_OP_CACHE_INVALIDATE       7u
#define MX_VMO_OP_CACHE_CLEAN            8u
#define MX_VMO_OP_CACHE_CLEAN_INVALIDATE 9u

// VM Object clone flags
#define MX_VMO_CLONE_COPY_ON_WRITE       1u

// Mapping flags to vmar routines
#define MX_VM_FLAG_PERM_READ          (1u << 0)
#define MX_VM_FLAG_PERM_WRITE         (1u << 1)
#define MX_VM_FLAG_PERM_EXECUTE       (1u << 2)
#define MX_VM_FLAG_COMPACT            (1u << 3)
#define MX_VM_FLAG_SPECIFIC           (1u << 4)
#define MX_VM_FLAG_SPECIFIC_OVERWRITE (1u << 5)
#define MX_VM_FLAG_CAN_MAP_SPECIFIC   (1u << 6)
#define MX_VM_FLAG_CAN_MAP_READ       (1u << 7)
#define MX_VM_FLAG_CAN_MAP_WRITE      (1u << 8)
#define MX_VM_FLAG_CAN_MAP_EXECUTE    (1u << 9)
#define MX_VM_FLAG_MAP_RANGE          (1u << 10)

// clock ids
#define MX_CLOCK_MONOTONIC        (0u)
#define MX_CLOCK_UTC              (1u)
#define MX_CLOCK_THREAD           (2u)

// virtual address
typedef uintptr_t mx_vaddr_t;

// physical address
typedef uintptr_t mx_paddr_t;

// offset
typedef uint64_t mx_off_t;
typedef int64_t mx_rel_off_t;

// Maximum string length for kernel names (process name, thread name, etc)
#define MX_MAX_NAME_LEN           (32)

// Buffer size limits on the cprng syscalls
#define MX_CPRNG_DRAW_MAX_LEN        256
#define MX_CPRNG_ADD_ENTROPY_MAX_LEN 256

// interrupt flags
#define MX_FLAG_REMAP_IRQ  0x1

// Channel options and limits.
#define MX_CHANNEL_READ_MAY_DISCARD         1u

#define MX_CHANNEL_MAX_MSG_BYTES            65536u
#define MX_CHANNEL_MAX_MSG_HANDLES          64u

// Socket options and limits.
// These options can be passed to mx_socket_write()
#define MX_SOCKET_SHUTDOWN_WRITE            (1u << 0)
#define MX_SOCKET_SHUTDOWN_READ             (1u << 1)
#define MX_SOCKET_SHUTDOWN_MASK             (MX_SOCKET_SHUTDOWN_WRITE | MX_SOCKET_SHUTDOWN_READ)
// These can be passed to mx_socket_create()
#define MX_SOCKET_STREAM                    (0u << 0)
#define MX_SOCKET_DATAGRAM                  (1u << 0)
#define MX_SOCKET_HAS_CONTROL               (1u << 1)
#define MX_SOCKET_CREATE_MASK               (MX_SOCKET_DATAGRAM | MX_SOCKET_HAS_CONTROL)
// These can be passed to mx_socket_read() and mx_socket_write().
#define MX_SOCKET_CONTROL                   (1u << 2)

// Flags which can be used to to control cache policy for APIs which map memory.
typedef enum {
    MX_CACHE_POLICY_CACHED          = 0,
    MX_CACHE_POLICY_UNCACHED        = 1,
    MX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    MX_CACHE_POLICY_WRITE_COMBINING = 3,

    MX_CACHE_POLICY_MASK            = 0x3,
} mx_cache_policy_t;

// Flag bits for mx_cache_flush.
#define MX_CACHE_FLUSH_INSN         (1u << 0)
#define MX_CACHE_FLUSH_DATA         (1u << 1)

// Timer options.
#define MX_TIMER_SLACK_CENTER       0u
#define MX_TIMER_SLACK_EARLY        1u
#define MX_TIMER_SLACK_LATE         2u


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
