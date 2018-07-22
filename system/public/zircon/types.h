// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TYPES_
#define ZIRCON_TYPES_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/limits.h>

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

typedef uint32_t zx_handle_t;

#define ZX_HANDLE_INVALID         ((zx_handle_t)0)

// See errors.h for the values zx_status_t can take.
typedef int32_t zx_status_t;

// absolute time in nanoseconds (generally with respect to the monotonic clock)
typedef uint64_t zx_time_t;
// a duration in nanoseconds
typedef uint64_t zx_duration_t;
// a duration in hardware ticks
typedef uint64_t zx_ticks_t;
#define ZX_TIME_INFINITE UINT64_MAX
#define ZX_NSEC(n) ((zx_duration_t)(1ULL * (n)))
#define ZX_USEC(n) ((zx_duration_t)(1000ULL * (n)))
#define ZX_MSEC(n) ((zx_duration_t)(1000000ULL * (n)))
#define ZX_SEC(n)  ((zx_duration_t)(1000000000ULL * (n)))
#define ZX_MIN(n)  (ZX_SEC(n) * 60ULL)
#define ZX_HOUR(n) (ZX_MIN(n) * 60ULL)

// clock ids
typedef uint32_t zx_clock_t;
#define ZX_CLOCK_MONOTONIC        ((zx_clock_t)0)
#define ZX_CLOCK_UTC              ((zx_clock_t)1)
#define ZX_CLOCK_THREAD           ((zx_clock_t)2)

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
#define __ZX_OBJECT_SIGNAL_22       ((zx_signals_t)1u << 22)
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

// Event
#define ZX_EVENT_SIGNALED           __ZX_OBJECT_SIGNALED
#define ZX_EVENT_SIGNAL_MASK        (ZX_USER_SIGNAL_ALL | __ZX_OBJECT_SIGNALED)

// EventPair
#define ZX_EVENTPAIR_SIGNALED       __ZX_OBJECT_SIGNALED
#define ZX_EVENTPAIR_PEER_CLOSED    __ZX_OBJECT_PEER_CLOSED
#define ZX_EVENTPAIR_SIGNAL_MASK    (ZX_USER_SIGNAL_ALL | __ZX_OBJECT_SIGNALED | __ZX_OBJECT_PEER_CLOSED)

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
#define ZX_SOCKET_ACCEPT            __ZX_OBJECT_SIGNAL_8
#define ZX_SOCKET_SHARE             __ZX_OBJECT_SIGNAL_9

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
#define ZX_THREAD_RUNNING           __ZX_OBJECT_SIGNAL_4
#define ZX_THREAD_SUSPENDED         __ZX_OBJECT_SIGNAL_5

// Log
#define ZX_LOG_READABLE             __ZX_OBJECT_READABLE
#define ZX_LOG_WRITABLE             __ZX_OBJECT_WRITABLE

// Timer
#define ZX_TIMER_SIGNALED           __ZX_OBJECT_SIGNALED

// VMO
#define ZX_VMO_ZERO_CHILDREN        __ZX_OBJECT_SIGNALED

// global kernel object id.
typedef uint64_t zx_koid_t;
#define ZX_KOID_INVALID ((uint64_t) 0)
#define ZX_KOID_KERNEL  ((uint64_t) 1)

// Transaction ID and argument types for zx_channel_call.
typedef uint32_t zx_txid_t;

typedef struct zx_channel_call_args {
    const void* wr_bytes;
    const zx_handle_t* wr_handles;
    void *rd_bytes;
    zx_handle_t* rd_handles;
    uint32_t wr_num_bytes;
    uint32_t wr_num_handles;
    uint32_t rd_num_bytes;
    uint32_t rd_num_handles;
} zx_channel_call_args_t;

// Maximum number of wait items allowed for zx_object_wait_many()
// TODO(ZX-1349) Re-lower this.
#define ZX_WAIT_MANY_MAX_ITEMS ((size_t)16)

// Structure for zx_object_wait_many():
typedef struct zx_wait_item {
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
#define ZX_RIGHT_WAIT             ((zx_rights_t)1u << 14)
#define ZX_RIGHT_INSPECT          ((zx_rights_t)1u << 15)
#define ZX_RIGHT_MANAGE_JOB       ((zx_rights_t)1u << 16)
#define ZX_RIGHT_MANAGE_PROCESS   ((zx_rights_t)1u << 17)
#define ZX_RIGHT_MANAGE_THREAD    ((zx_rights_t)1u << 18)
#define ZX_RIGHT_APPLY_PROFILE    ((zx_rights_t)1u << 19)
#define ZX_RIGHT_SAME_RIGHTS      ((zx_rights_t)1u << 31)

// Convenient names for commonly grouped rights
#define ZX_RIGHTS_BASIC \
    (ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE |\
     ZX_RIGHT_WAIT | ZX_RIGHT_INSPECT)

#define ZX_RIGHTS_IO \
    (ZX_RIGHT_READ | ZX_RIGHT_WRITE)

#define ZX_RIGHTS_PROPERTY \
    (ZX_RIGHT_GET_PROPERTY | ZX_RIGHT_SET_PROPERTY)

#define ZX_RIGHTS_POLICY \
    (ZX_RIGHT_GET_POLICY | ZX_RIGHT_SET_POLICY)

// VM Object creation options
#define ZX_VMO_NON_RESIZABLE             ((uint32_t)1u)

// VM Object opcodes
#define ZX_VMO_OP_COMMIT                 ((uint32_t)1u)
#define ZX_VMO_OP_DECOMMIT               ((uint32_t)2u)
#define ZX_VMO_OP_LOCK                   ((uint32_t)3u)
#define ZX_VMO_OP_UNLOCK                 ((uint32_t)4u)
// opcode 5 was ZX_VMO_OP_LOOKUP, but is now unused.
#define ZX_VMO_OP_CACHE_SYNC             ((uint32_t)6u)
#define ZX_VMO_OP_CACHE_INVALIDATE       ((uint32_t)7u)
#define ZX_VMO_OP_CACHE_CLEAN            ((uint32_t)8u)
#define ZX_VMO_OP_CACHE_CLEAN_INVALIDATE ((uint32_t)9u)

// VM Object clone flags
#define ZX_VMO_CLONE_COPY_ON_WRITE        ((uint32_t)1u << 0)
#define ZX_VMO_CLONE_NON_RESIZEABLE       ((uint32_t)1u << 1)

// Mapping flags to vmar routines
#define ZX_VM_FLAG_PERM_READ              ((uint32_t)1u << 0)
#define ZX_VM_FLAG_PERM_WRITE             ((uint32_t)1u << 1)
#define ZX_VM_FLAG_PERM_EXECUTE           ((uint32_t)1u << 2)
#define ZX_VM_FLAG_COMPACT                ((uint32_t)1u << 3)
#define ZX_VM_FLAG_SPECIFIC               ((uint32_t)1u << 4)
#define ZX_VM_FLAG_SPECIFIC_OVERWRITE     ((uint32_t)1u << 5)
#define ZX_VM_FLAG_CAN_MAP_SPECIFIC       ((uint32_t)1u << 6)
#define ZX_VM_FLAG_CAN_MAP_READ           ((uint32_t)1u << 7)
#define ZX_VM_FLAG_CAN_MAP_WRITE          ((uint32_t)1u << 8)
#define ZX_VM_FLAG_CAN_MAP_EXECUTE        ((uint32_t)1u << 9)
#define ZX_VM_FLAG_MAP_RANGE              ((uint32_t)1u << 10)
#define ZX_VM_FLAG_REQUIRE_NON_RESIZABLE  ((uint32_t)1u << 11)

// virtual address
typedef uintptr_t zx_vaddr_t;

// physical address
typedef uintptr_t zx_paddr_t;
// low mem physical address
typedef uint32_t  zx_paddr32_t;
// Hypervisor guest physical addresses.
typedef uintptr_t zx_gpaddr_t;

// offset
typedef uint64_t zx_off_t;

// Maximum string length for kernel names (process name, thread name, etc)
#define ZX_MAX_NAME_LEN              ((size_t)32u)

// Buffer size limits on the cprng syscalls
#define ZX_CPRNG_DRAW_MAX_LEN        ((size_t)256u)
#define ZX_CPRNG_ADD_ENTROPY_MAX_LEN ((size_t)256u)

// interrupt bind flags
#define ZX_INTERRUPT_REMAP_IRQ       ((uint32_t)0x1u)
#define ZX_INTERRUPT_MODE_DEFAULT    ((uint32_t)0u << 1)
#define ZX_INTERRUPT_MODE_EDGE_LOW   ((uint32_t)1u << 1)
#define ZX_INTERRUPT_MODE_EDGE_HIGH  ((uint32_t)2u << 1)
#define ZX_INTERRUPT_MODE_LEVEL_LOW  ((uint32_t)3u << 1)
#define ZX_INTERRUPT_MODE_LEVEL_HIGH ((uint32_t)4u << 1)
#define ZX_INTERRUPT_MODE_EDGE_BOTH  ((uint32_t)5u << 1)
#define ZX_INTERRUPT_MODE_MASK       ((uint32_t)0xe)
#define ZX_INTERRUPT_VIRTUAL         ((uint32_t)0x10)

// Preallocated virtual interrupt slot, typically used for signaling interrupt threads to exit.
#define ZX_INTERRUPT_SLOT_USER              ((uint32_t)62u)
// interrupt wait slots must be in the range 0 - 62 inclusive
#define ZX_INTERRUPT_MAX_SLOTS              ((uint32_t)62u)

// PCI interrupt handles use interrupt slot 0 for the PCI hardware interrupt
#define ZX_PCI_INTERRUPT_SLOT               ((uint32_t)0u)

// Channel options and limits.
#define ZX_CHANNEL_READ_MAY_DISCARD         ((uint32_t)1u)

#define ZX_CHANNEL_MAX_MSG_BYTES            ((uint32_t)65536u)
#define ZX_CHANNEL_MAX_MSG_HANDLES          ((uint32_t)64u)

// Socket options and limits.
// These options can be passed to zx_socket_write()
#define ZX_SOCKET_SHUTDOWN_WRITE            ((uint32_t)1u << 0)
#define ZX_SOCKET_SHUTDOWN_READ             ((uint32_t)1u << 1)
#define ZX_SOCKET_SHUTDOWN_MASK             (ZX_SOCKET_SHUTDOWN_WRITE | ZX_SOCKET_SHUTDOWN_READ)

// These can be passed to zx_socket_create()
#define ZX_SOCKET_STREAM                    ((uint32_t)0u)
#define ZX_SOCKET_DATAGRAM                  ((uint32_t)1u << 0)
#define ZX_SOCKET_HAS_CONTROL               ((uint32_t)1u << 1)
#define ZX_SOCKET_HAS_ACCEPT                ((uint32_t)1u << 2)
#define ZX_SOCKET_CREATE_MASK               (ZX_SOCKET_DATAGRAM | ZX_SOCKET_HAS_CONTROL | ZX_SOCKET_HAS_ACCEPT)

// These can be passed to zx_socket_read() and zx_socket_write().
#define ZX_SOCKET_CONTROL                   ((uint32_t)1u << 2)

// Flags which can be used to to control cache policy for APIs which map memory.
#define ZX_CACHE_POLICY_CACHED              ((uint32_t)0u)
#define ZX_CACHE_POLICY_UNCACHED            ((uint32_t)1u)
#define ZX_CACHE_POLICY_UNCACHED_DEVICE     ((uint32_t)2u)
#define ZX_CACHE_POLICY_WRITE_COMBINING     ((uint32_t)3u)
#define ZX_CACHE_POLICY_MASK                ((uint32_t)3u)

// Flag bits for zx_cache_flush.
#define ZX_CACHE_FLUSH_INSN         ((uint32_t)1u << 0)
#define ZX_CACHE_FLUSH_DATA         ((uint32_t)1u << 1)
#define ZX_CACHE_FLUSH_INVALIDATE   ((uint32_t)1u << 2)

// Timer options.
#define ZX_TIMER_SLACK_CENTER       ((uint32_t)0u)
#define ZX_TIMER_SLACK_EARLY        ((uint32_t)1u)
#define ZX_TIMER_SLACK_LATE         ((uint32_t)2u)

// Bus Transaction Initiatior options.
#define ZX_BTI_PERM_READ          ((uint32_t)1u << 0)
#define ZX_BTI_PERM_WRITE         ((uint32_t)1u << 1)
#define ZX_BTI_PERM_EXECUTE       ((uint32_t)1u << 2)
#define ZX_BTI_COMPRESS           ((uint32_t)1u << 3)

typedef uint32_t zx_obj_type_t;

#define ZX_OBJ_TYPE_NONE            ((zx_obj_type_t)0u)
#define ZX_OBJ_TYPE_PROCESS         ((zx_obj_type_t)1u)
#define ZX_OBJ_TYPE_THREAD          ((zx_obj_type_t)2u)
#define ZX_OBJ_TYPE_VMO             ((zx_obj_type_t)3u)
#define ZX_OBJ_TYPE_CHANNEL         ((zx_obj_type_t)4u)
#define ZX_OBJ_TYPE_EVENT           ((zx_obj_type_t)5u)
#define ZX_OBJ_TYPE_PORT            ((zx_obj_type_t)6u)
#define ZX_OBJ_TYPE_INTERRUPT       ((zx_obj_type_t)9u)
#define ZX_OBJ_TYPE_PCI_DEVICE      ((zx_obj_type_t)11u)
#define ZX_OBJ_TYPE_LOG             ((zx_obj_type_t)12u)
#define ZX_OBJ_TYPE_SOCKET          ((zx_obj_type_t)14u)
#define ZX_OBJ_TYPE_RESOURCE        ((zx_obj_type_t)15u)
#define ZX_OBJ_TYPE_EVENTPAIR       ((zx_obj_type_t)16u)
#define ZX_OBJ_TYPE_JOB             ((zx_obj_type_t)17u)
#define ZX_OBJ_TYPE_VMAR            ((zx_obj_type_t)18u)
#define ZX_OBJ_TYPE_FIFO            ((zx_obj_type_t)19u)
#define ZX_OBJ_TYPE_GUEST           ((zx_obj_type_t)20u)
#define ZX_OBJ_TYPE_VCPU            ((zx_obj_type_t)21u)
#define ZX_OBJ_TYPE_TIMER           ((zx_obj_type_t)22u)
#define ZX_OBJ_TYPE_IOMMU           ((zx_obj_type_t)23u)
#define ZX_OBJ_TYPE_BTI             ((zx_obj_type_t)24u)
#define ZX_OBJ_TYPE_PROFILE         ((zx_obj_type_t)25u)
#define ZX_OBJ_TYPE_PMT             ((zx_obj_type_t)26u)
#define ZX_OBJ_TYPE_SUSPEND_TOKEN   ((zx_obj_type_t)27u)
#define ZX_OBJ_TYPE_LAST            ((zx_obj_type_t)28u)

typedef struct zx_handle_info {
    zx_handle_t handle;
    zx_obj_type_t type;
    zx_rights_t rights;
    uint32_t unused;
} zx_handle_info_t;

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

#endif // ZIRCON_TYPES_
