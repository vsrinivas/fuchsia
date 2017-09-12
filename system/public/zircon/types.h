// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/errors.h>
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
typedef uint32_t zx_handle_t;
#else
typedef int32_t zx_handle_t;
#endif

#define ZX_HANDLE_INVALID         ((zx_handle_t)0)

// Same as kernel status_t
typedef int32_t zx_status_t;

// absolute time in nanoseconds (generally with respect to the monotonic clock)
typedef uint64_t zx_time_t;
// a duration in nanoseconds
typedef uint64_t zx_duration_t;
#define ZX_TIME_INFINITE UINT64_MAX
#define ZX_USEC(n) ((zx_duration_t)(1000ULL * (n)))
#define ZX_MSEC(n) ((zx_duration_t)(1000000ULL * (n)))
#define ZX_SEC(n)  ((zx_duration_t)(1000000000ULL * (n)))

typedef uint32_t zx_signals_t;

#define ZX_SIGNAL_NONE              ((zx_signals_t)0u)
#define ZX_USER_SIGNAL_ALL          ((zx_signals_t)0xff000000u)

// Implementation details (__ZX_* not intended for public consumption)
//
// Signals that have a common meaning where used are named with that
// meaning.  Signals that do not, or are not yet in use, are named
// generically.
#define __ZX_OBJECT_SIGNAL_ALL      ((zx_signals_t)0x00ffffffu)
#define __ZX_OBJECT_READABLE        ((zx_signals_t)1u << 0)
#define __ZX_OBJECT_WRITABLE        ((zx_signals_t)1u << 1)
#define __ZX_OBJECT_PEER_CLOSED     ((zx_signals_t)1u << 2)
#define __ZX_OBJECT_SIGNALED        ((zx_signals_t)1u << 3)
#define __ZX_OBJECT_SIGNAL_4        ((zx_signals_t)1u << 4)
#define __ZX_OBJECT_SIGNAL_5        ((zx_signals_t)1u << 5)
#define __ZX_OBJECT_SIGNAL_6        ((zx_signals_t)1u << 6)
#define __ZX_OBJECT_SIGNAL_7        ((zx_signals_t)1u << 7)
#define __ZX_OBJECT_SIGNAL_8        ((zx_signals_t)1u << 8)
#define __ZX_OBJECT_SIGNAL_9        ((zx_signals_t)1u << 9)
#define __ZX_OBJECT_SIGNAL_10       ((zx_signals_t)1u << 10)
#define __ZX_OBJECT_SIGNAL_11       ((zx_signals_t)1u << 11)
#define __ZX_OBJECT_SIGNAL_12       ((zx_signals_t)1u << 12)
#define __ZX_OBJECT_SIGNAL_13       ((zx_signals_t)1u << 13)
#define __ZX_OBJECT_SIGNAL_14       ((zx_signals_t)1u << 14)
#define __ZX_OBJECT_SIGNAL_15       ((zx_signals_t)1u << 15)
#define __ZX_OBJECT_SIGNAL_16       ((zx_signals_t)1u << 16)
#define __ZX_OBJECT_SIGNAL_17       ((zx_signals_t)1u << 17)
#define __ZX_OBJECT_SIGNAL_18       ((zx_signals_t)1u << 18)
#define __ZX_OBJECT_SIGNAL_19       ((zx_signals_t)1u << 19)
#define __ZX_OBJECT_SIGNAL_20       ((zx_signals_t)1u << 20)
#define __ZX_OBJECT_SIGNAL_21       ((zx_signals_t)1u << 21)
#define __ZX_OBJECT_LAST_HANDLE     ((zx_signals_t)1u << 22)
#define __ZX_OBJECT_HANDLE_CLOSED   ((zx_signals_t)1u << 23)



// User Signals (for zx_object_signal() and zx_object_signal_peer())
#define ZX_USER_SIGNAL_0            ((zx_signals_t)1u << 24)
#define ZX_USER_SIGNAL_1            ((zx_signals_t)1u << 25)
#define ZX_USER_SIGNAL_2            ((zx_signals_t)1u << 26)
#define ZX_USER_SIGNAL_3            ((zx_signals_t)1u << 27)
#define ZX_USER_SIGNAL_4            ((zx_signals_t)1u << 28)
#define ZX_USER_SIGNAL_5            ((zx_signals_t)1u << 29)
#define ZX_USER_SIGNAL_6            ((zx_signals_t)1u << 30)
#define ZX_USER_SIGNAL_7            ((zx_signals_t)1u << 31)

// Cancelation (handle was closed while waiting with it)
#define ZX_SIGNAL_HANDLE_CLOSED     __ZX_OBJECT_HANDLE_CLOSED

// Only one user-more reference (handle) to the object exists.
#define ZX_SIGNAL_LAST_HANDLE       __ZX_OBJECT_LAST_HANDLE

// Event
#define ZX_EVENT_SIGNALED           __ZX_OBJECT_SIGNALED
#define ZX_EVENT_SIGNAL_MASK        (ZX_USER_SIGNAL_ALL | __ZX_OBJECT_SIGNALED)

// EventPair
#define ZX_EPAIR_SIGNALED           __ZX_OBJECT_SIGNALED
#define ZX_EPAIR_PEER_CLOSED        __ZX_OBJECT_PEER_CLOSED
#define ZX_EPAIR_SIGNAL_MASK        (ZX_USER_SIGNAL_ALL | __ZX_OBJECT_SIGNALED | __ZX_OBJECT_PEER_CLOSED)

// Channel
#define ZX_CHANNEL_READABLE         __ZX_OBJECT_READABLE
#define ZX_CHANNEL_WRITABLE         __ZX_OBJECT_WRITABLE
#define ZX_CHANNEL_PEER_CLOSED      __ZX_OBJECT_PEER_CLOSED

// Socket
#define ZX_SOCKET_READABLE          __ZX_OBJECT_READABLE
#define ZX_SOCKET_WRITABLE          __ZX_OBJECT_WRITABLE
#define ZX_SOCKET_PEER_CLOSED       __ZX_OBJECT_PEER_CLOSED
#define ZX_SOCKET_READ_DISABLED     __ZX_OBJECT_SIGNAL_4
#define ZX_SOCKET_WRITE_DISABLED    __ZX_OBJECT_SIGNAL_5
#define ZX_SOCKET_CONTROL_READABLE  __ZX_OBJECT_SIGNAL_6
#define ZX_SOCKET_CONTROL_WRITABLE  __ZX_OBJECT_SIGNAL_7

// Port
#define ZX_PORT_READABLE            __ZX_OBJECT_READABLE

// Fifo
#define ZX_FIFO_READABLE            __ZX_OBJECT_READABLE
#define ZX_FIFO_WRITABLE            __ZX_OBJECT_WRITABLE
#define ZX_FIFO_PEER_CLOSED         __ZX_OBJECT_PEER_CLOSED

// Task signals (process, thread, job)
#define ZX_TASK_TERMINATED          __ZX_OBJECT_SIGNALED

// Job
#define ZX_JOB_NO_PROCESSES         __ZX_OBJECT_SIGNALED
#define ZX_JOB_NO_JOBS              __ZX_OBJECT_SIGNAL_4

// Process
#define ZX_PROCESS_TERMINATED       __ZX_OBJECT_SIGNALED

// Thread
#define ZX_THREAD_TERMINATED        __ZX_OBJECT_SIGNALED

// Log
#define ZX_LOG_READABLE             __ZX_OBJECT_READABLE
#define ZX_LOG_WRITABLE             __ZX_OBJECT_WRITABLE

// Timer
#define ZX_TIMER_SIGNALED           __ZX_OBJECT_SIGNALED

// global kernel object id.
typedef uint64_t zx_koid_t;
#define ZX_KOID_INVALID ((uint64_t) 0)
#define ZX_KOID_KERNEL  ((uint64_t) 1)

// Transaction ID and argument types for zx_channel_call.
typedef uint32_t zx_txid_t;

typedef struct {
    const void* wr_bytes;
    const zx_handle_t* wr_handles;
    void *rd_bytes;
    zx_handle_t* rd_handles;
    uint32_t wr_num_bytes;
    uint32_t wr_num_handles;
    uint32_t rd_num_bytes;
    uint32_t rd_num_handles;
} zx_channel_call_args_t;

// Structure for zx_object_wait_many():
typedef struct {
    zx_handle_t handle;
    zx_signals_t waitfor;
    zx_signals_t pending;
} zx_wait_item_t;

typedef uint32_t zx_rights_t;
#define ZX_RIGHT_NONE             ((zx_rights_t)0u)
#define ZX_RIGHT_DUPLICATE        ((zx_rights_t)1u << 0)
#define ZX_RIGHT_TRANSFER         ((zx_rights_t)1u << 1)
#define ZX_RIGHT_READ             ((zx_rights_t)1u << 2)
#define ZX_RIGHT_WRITE            ((zx_rights_t)1u << 3)
#define ZX_RIGHT_EXECUTE          ((zx_rights_t)1u << 4)
#define ZX_RIGHT_MAP              ((zx_rights_t)1u << 5)
#define ZX_RIGHT_GET_PROPERTY     ((zx_rights_t)1u << 6)
#define ZX_RIGHT_SET_PROPERTY     ((zx_rights_t)1u << 7)
#define ZX_RIGHT_ENUMERATE        ((zx_rights_t)1u << 8)
#define ZX_RIGHT_DESTROY          ((zx_rights_t)1u << 9)
#define ZX_RIGHT_SET_POLICY       ((zx_rights_t)1u << 10)
#define ZX_RIGHT_GET_POLICY       ((zx_rights_t)1u << 11)
#define ZX_RIGHT_SIGNAL           ((zx_rights_t)1u << 12)
#define ZX_RIGHT_SIGNAL_PEER      ((zx_rights_t)1u << 13)

#define ZX_RIGHT_SAME_RIGHTS      ((zx_rights_t)1u << 31)

// VM Object opcodes
#define ZX_VMO_OP_COMMIT                 1u
#define ZX_VMO_OP_DECOMMIT               2u
#define ZX_VMO_OP_LOCK                   3u
#define ZX_VMO_OP_UNLOCK                 4u
#define ZX_VMO_OP_LOOKUP                 5u
#define ZX_VMO_OP_CACHE_SYNC             6u
#define ZX_VMO_OP_CACHE_INVALIDATE       7u
#define ZX_VMO_OP_CACHE_CLEAN            8u
#define ZX_VMO_OP_CACHE_CLEAN_INVALIDATE 9u

// VM Object clone flags
#define ZX_VMO_CLONE_COPY_ON_WRITE       1u

// Mapping flags to vmar routines
#define ZX_VM_FLAG_PERM_READ          (1u << 0)
#define ZX_VM_FLAG_PERM_WRITE         (1u << 1)
#define ZX_VM_FLAG_PERM_EXECUTE       (1u << 2)
#define ZX_VM_FLAG_COMPACT            (1u << 3)
#define ZX_VM_FLAG_SPECIFIC           (1u << 4)
#define ZX_VM_FLAG_SPECIFIC_OVERWRITE (1u << 5)
#define ZX_VM_FLAG_CAN_MAP_SPECIFIC   (1u << 6)
#define ZX_VM_FLAG_CAN_MAP_READ       (1u << 7)
#define ZX_VM_FLAG_CAN_MAP_WRITE      (1u << 8)
#define ZX_VM_FLAG_CAN_MAP_EXECUTE    (1u << 9)
#define ZX_VM_FLAG_MAP_RANGE          (1u << 10)

// clock ids
#define ZX_CLOCK_MONOTONIC        (0u)
#define ZX_CLOCK_UTC              (1u)
#define ZX_CLOCK_THREAD           (2u)

// virtual address
typedef uintptr_t zx_vaddr_t;

// physical address
typedef uintptr_t zx_paddr_t;

// offset
typedef uint64_t zx_off_t;
typedef int64_t zx_rel_off_t;

// Maximum string length for kernel names (process name, thread name, etc)
#define ZX_MAX_NAME_LEN           (32)

// Buffer size limits on the cprng syscalls
#define ZX_CPRNG_DRAW_MAX_LEN        256
#define ZX_CPRNG_ADD_ENTROPY_MAX_LEN 256

// interrupt flags
#define ZX_FLAG_REMAP_IRQ  0x1

// Channel options and limits.
#define ZX_CHANNEL_READ_MAY_DISCARD         1u

#define ZX_CHANNEL_MAX_MSG_BYTES            65536u
#define ZX_CHANNEL_MAX_MSG_HANDLES          64u

// Socket options and limits.
// These options can be passed to zx_socket_write()
#define ZX_SOCKET_SHUTDOWN_WRITE            (1u << 0)
#define ZX_SOCKET_SHUTDOWN_READ             (1u << 1)
#define ZX_SOCKET_SHUTDOWN_MASK             (ZX_SOCKET_SHUTDOWN_WRITE | ZX_SOCKET_SHUTDOWN_READ)
// These can be passed to zx_socket_create()
#define ZX_SOCKET_STREAM                    (0u << 0)
#define ZX_SOCKET_DATAGRAM                  (1u << 0)
#define ZX_SOCKET_HAS_CONTROL               (1u << 1)
#define ZX_SOCKET_CREATE_MASK               (ZX_SOCKET_DATAGRAM | ZX_SOCKET_HAS_CONTROL)
// These can be passed to zx_socket_read() and zx_socket_write().
#define ZX_SOCKET_CONTROL                   (1u << 2)

// Flags which can be used to to control cache policy for APIs which map memory.
typedef enum {
    ZX_CACHE_POLICY_CACHED          = 0,
    ZX_CACHE_POLICY_UNCACHED        = 1,
    ZX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    ZX_CACHE_POLICY_WRITE_COMBINING = 3,

    ZX_CACHE_POLICY_MASK            = 0x3,
} zx_cache_policy_t;

// Flag bits for zx_cache_flush.
#define ZX_CACHE_FLUSH_INSN         (1u << 0)
#define ZX_CACHE_FLUSH_DATA         (1u << 1)

// Timer options.
#define ZX_TIMER_SLACK_CENTER       0u
#define ZX_TIMER_SLACK_EARLY        1u
#define ZX_TIMER_SLACK_LATE         2u


#ifdef __cplusplus
// We cannot use <stdatomic.h> with C++ code as _Atomic qualifier defined by
// C11 is not valid in C++11. There is not a single standard name that can
// be used in both C and C++. C++ <atomic> defines names which are equivalent
// to those in <stdatomic.h>, but these are contained in the std namespace.
//
// In kernel, the only operation done is a user_copy (of sizeof(int)) inside a
// lock; otherwise the futex address is treated as a key.
typedef int zx_futex_t;
#else
#ifdef _KERNEL
typedef int zx_futex_t;
#else
typedef atomic_int zx_futex_t;
#endif
#endif

__END_CDECLS
