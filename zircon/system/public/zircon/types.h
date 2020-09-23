// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_TYPES_H_
#define SYSROOT_ZIRCON_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/time.h>

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

#define ZX_HANDLE_INVALID           ((zx_handle_t)0)
#define ZX_HANDLE_FIXED_BITS_MASK   ((zx_handle_t)0x3)

// See errors.h for the values zx_status_t can take.
typedef int32_t zx_status_t;

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

// Cancellation (handle was closed while waiting with it)
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

// Clock
#define ZX_CLOCK_STARTED            __ZX_OBJECT_SIGNAL_4

// Socket
#define ZX_SOCKET_READABLE            __ZX_OBJECT_READABLE
#define ZX_SOCKET_WRITABLE            __ZX_OBJECT_WRITABLE
#define ZX_SOCKET_PEER_CLOSED         __ZX_OBJECT_PEER_CLOSED
#define ZX_SOCKET_PEER_WRITE_DISABLED __ZX_OBJECT_SIGNAL_4
#define ZX_SOCKET_WRITE_DISABLED      __ZX_OBJECT_SIGNAL_5
#define ZX_SOCKET_READ_THRESHOLD      __ZX_OBJECT_SIGNAL_10
#define ZX_SOCKET_WRITE_THRESHOLD     __ZX_OBJECT_SIGNAL_11

// Fifo
#define ZX_FIFO_READABLE            __ZX_OBJECT_READABLE
#define ZX_FIFO_WRITABLE            __ZX_OBJECT_WRITABLE
#define ZX_FIFO_PEER_CLOSED         __ZX_OBJECT_PEER_CLOSED

// Task signals (process, thread, job)
#define ZX_TASK_TERMINATED          __ZX_OBJECT_SIGNALED

// Job
#define ZX_JOB_TERMINATED           __ZX_OBJECT_SIGNALED
#define ZX_JOB_NO_JOBS              __ZX_OBJECT_SIGNAL_4
#define ZX_JOB_NO_PROCESSES         __ZX_OBJECT_SIGNAL_5

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
// Note: kernel object ids use 63 bits, with the most significant bit being zero.
// The remaining values (msb==1) are for use by programs and tools that wish to
// create koids for artificial objects.
typedef uint64_t zx_koid_t;
#define ZX_KOID_INVALID ((uint64_t) 0)
#define ZX_KOID_KERNEL  ((uint64_t) 1)
// The first non-reserved koid. The first 1024 are reserved.
#define ZX_KOID_FIRST   ((uint64_t) 1024)

// Maximum number of wait items allowed for zx_object_wait_many()
#define ZX_WAIT_MANY_MAX_ITEMS ((size_t)64)

// Structure for zx_object_wait_many():
typedef struct zx_wait_item {
    zx_handle_t handle;
    zx_signals_t waitfor;
    zx_signals_t pending;
} zx_wait_item_t;

// VM Object creation options
#define ZX_VMO_RESIZABLE                 ((uint32_t)1u << 1)

// VM Object opcodes
#define ZX_VMO_OP_COMMIT                 ((uint32_t)1u)
// Keep value in sync with ZX_VMAR_OP_DECOMMIT.
#define ZX_VMO_OP_DECOMMIT               ((uint32_t)2u)
#define ZX_VMO_OP_LOCK                   ((uint32_t)3u)
#define ZX_VMO_OP_UNLOCK                 ((uint32_t)4u)
// opcode 5 was ZX_VMO_OP_LOOKUP, but is now unused.
#define ZX_VMO_OP_CACHE_SYNC             ((uint32_t)6u)
#define ZX_VMO_OP_CACHE_INVALIDATE       ((uint32_t)7u)
#define ZX_VMO_OP_CACHE_CLEAN            ((uint32_t)8u)
#define ZX_VMO_OP_CACHE_CLEAN_INVALIDATE ((uint32_t)9u)
#define ZX_VMO_OP_ZERO                   ((uint32_t)10u)

// VMAR opcodes
// Keep value in sync with ZX_VMO_OP_DECOMMIT.
#define ZX_VMAR_OP_DECOMMIT              ((uint32_t)2u)
#define ZX_VMAR_OP_MAP_RANGE             ((uint32_t)3u)

// Pager opcodes
#define ZX_PAGER_OP_FAIL                 ((uint32_t)1u)

// VM Object clone flags
#define ZX_VMO_CHILD_SNAPSHOT             ((uint32_t)1u << 0)
#define ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE ((uint32_t)1u << 4)
#define ZX_VMO_CHILD_RESIZABLE            ((uint32_t)1u << 2)
#define ZX_VMO_CHILD_SLICE                ((uint32_t)1u << 3)
#define ZX_VMO_CHILD_NO_WRITE             ((uint32_t)1u << 5)
// Old clone flags that are on the path to deprecation.
#define ZX_VMO_CLONE_COPY_ON_WRITE        ((uint32_t)1u << 4)
#define ZX_VMO_CHILD_COPY_ON_WRITE        ((uint32_t)1u << 4)
#define ZX_VMO_CHILD_PRIVATE_PAGER_COPY   ((uint32_t)1u << 4)

typedef uint32_t zx_vm_option_t;
// Mapping flags to vmar routines
#define ZX_VM_PERM_READ             ((zx_vm_option_t)(1u << 0))
#define ZX_VM_PERM_WRITE            ((zx_vm_option_t)(1u << 1))
#define ZX_VM_PERM_EXECUTE          ((zx_vm_option_t)(1u << 2))
#define ZX_VM_COMPACT               ((zx_vm_option_t)(1u << 3))
#define ZX_VM_SPECIFIC              ((zx_vm_option_t)(1u << 4))
#define ZX_VM_SPECIFIC_OVERWRITE    ((zx_vm_option_t)(1u << 5))
#define ZX_VM_CAN_MAP_SPECIFIC      ((zx_vm_option_t)(1u << 6))
#define ZX_VM_CAN_MAP_READ          ((zx_vm_option_t)(1u << 7))
#define ZX_VM_CAN_MAP_WRITE         ((zx_vm_option_t)(1u << 8))
#define ZX_VM_CAN_MAP_EXECUTE       ((zx_vm_option_t)(1u << 9))
#define ZX_VM_MAP_RANGE             ((zx_vm_option_t)(1u << 10))
#define ZX_VM_REQUIRE_NON_RESIZABLE ((zx_vm_option_t)(1u << 11))
#define ZX_VM_ALLOW_FAULTS          ((zx_vm_option_t)(1u << 12))
#define ZX_VM_OFFSET_IS_UPPER_LIMIT ((zx_vm_option_t)(1u << 13))

#define ZX_VM_ALIGN_BASE            24
#define ZX_VM_ALIGN_1KB             ((zx_vm_option_t)(10u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_2KB             ((zx_vm_option_t)(11u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_4KB             ((zx_vm_option_t)(12u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_8KB             ((zx_vm_option_t)(13u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_16KB            ((zx_vm_option_t)(14u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_32KB            ((zx_vm_option_t)(15u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_64KB            ((zx_vm_option_t)(16u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_128KB           ((zx_vm_option_t)(17u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_256KB           ((zx_vm_option_t)(18u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_512KB           ((zx_vm_option_t)(19u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_1MB             ((zx_vm_option_t)(20u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_2MB             ((zx_vm_option_t)(21u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_4MB             ((zx_vm_option_t)(22u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_8MB             ((zx_vm_option_t)(23u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_16MB            ((zx_vm_option_t)(24u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_32MB            ((zx_vm_option_t)(25u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_64MB            ((zx_vm_option_t)(26u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_128MB           ((zx_vm_option_t)(27u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_256MB           ((zx_vm_option_t)(28u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_512MB           ((zx_vm_option_t)(29u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_1GB             ((zx_vm_option_t)(30u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_2GB             ((zx_vm_option_t)(31u << ZX_VM_ALIGN_BASE))
#define ZX_VM_ALIGN_4GB             ((zx_vm_option_t)(32u << ZX_VM_ALIGN_BASE))

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

// vectorized I/O
typedef struct zx_iovec {
  void* buffer;
  size_t capacity;
} zx_iovec_t;

// Maximum string length for kernel names (process name, thread name, etc)
// TODO(fxbug.dev/7802): This must be manually kept in sync with zx_common.fidl.
// Eventually (some of) this file will be generated from //zircon/vdso.
#define ZX_MAX_NAME_LEN              ((size_t)32u)

// Buffer size limits on the cprng syscalls
#define ZX_CPRNG_DRAW_MAX_LEN        ((size_t)256u)
#define ZX_CPRNG_ADD_ENTROPY_MAX_LEN ((size_t)256u)

// interrupt_create flags
#define ZX_INTERRUPT_REMAP_IRQ       ((uint32_t)0x1u)
#define ZX_INTERRUPT_MODE_DEFAULT    ((uint32_t)0u << 1)
#define ZX_INTERRUPT_MODE_EDGE_LOW   ((uint32_t)1u << 1)
#define ZX_INTERRUPT_MODE_EDGE_HIGH  ((uint32_t)2u << 1)
#define ZX_INTERRUPT_MODE_LEVEL_LOW  ((uint32_t)3u << 1)
#define ZX_INTERRUPT_MODE_LEVEL_HIGH ((uint32_t)4u << 1)
#define ZX_INTERRUPT_MODE_EDGE_BOTH  ((uint32_t)5u << 1)
#define ZX_INTERRUPT_MODE_MASK       ((uint32_t)0xe)
#define ZX_INTERRUPT_VIRTUAL         ((uint32_t)0x10)

// interrupt_bind flags
#define ZX_INTERRUPT_BIND            ((uint32_t)0x0u)
#define ZX_INTERRUPT_UNBIND          ((uint32_t)0x1u)

// Preallocated virtual interrupt slot, typically used for signaling interrupt threads to exit.
#define ZX_INTERRUPT_SLOT_USER              ((uint32_t)62u)
// interrupt wait slots must be in the range 0 - 62 inclusive
#define ZX_INTERRUPT_MAX_SLOTS              ((uint32_t)62u)

// msi_create flags
#define ZX_MSI_MODE_MSI_X                   ((uint32_t)0x1u)

// PCI interrupt handles use interrupt slot 0 for the PCI hardware interrupt
#define ZX_PCI_INTERRUPT_SLOT               ((uint32_t)0u)

// Channel options and limits.
#define ZX_CHANNEL_READ_MAY_DISCARD         ((uint32_t)1u)

// TODO(fxbug.dev/7802): This must be manually kept in sync with zx_common.fidl.
// Eventually (some of) this file will be generated from //zircon/vdso.
#define ZX_CHANNEL_MAX_MSG_BYTES            ((uint32_t)65536u)
#define ZX_CHANNEL_MAX_MSG_HANDLES          ((uint32_t)64u)

// Fifo limits.
#define ZX_FIFO_MAX_SIZE_BYTES              ZX_PAGE_SIZE

// Socket options and limits.
// These options can be passed to zx_socket_shutdown().
#define ZX_SOCKET_SHUTDOWN_WRITE            ((uint32_t)1u << 0)
#define ZX_SOCKET_SHUTDOWN_READ             ((uint32_t)1u << 1)
#define ZX_SOCKET_SHUTDOWN_MASK             (ZX_SOCKET_SHUTDOWN_WRITE | ZX_SOCKET_SHUTDOWN_READ)

// These can be passed to zx_socket_create().
#define ZX_SOCKET_STREAM                    ((uint32_t)0u)
#define ZX_SOCKET_DATAGRAM                  ((uint32_t)1u << 0)
#define ZX_SOCKET_CREATE_MASK               (ZX_SOCKET_DATAGRAM)

// These can be passed to zx_socket_read().
#define ZX_SOCKET_PEEK                      ((uint32_t)1u << 3)

// These can be passed to zx_stream_create().
#define ZX_STREAM_MODE_READ                 ((uint32_t)1u << 0)
#define ZX_STREAM_MODE_WRITE                ((uint32_t)1u << 1)
#define ZX_STREAM_CREATE_MASK               (ZX_STREAM_MODE_READ | ZX_STREAM_MODE_WRITE)

// These can be passed to zx_stream_writev().
#define ZX_STREAM_APPEND                    ((uint32_t)1u << 0)

typedef uint32_t zx_stream_seek_origin_t;
#define ZX_STREAM_SEEK_ORIGIN_START      ((zx_stream_seek_origin_t)0u)
#define ZX_STREAM_SEEK_ORIGIN_CURRENT    ((zx_stream_seek_origin_t)1u)
#define ZX_STREAM_SEEK_ORIGIN_END        ((zx_stream_seek_origin_t)2u)

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

// Bus Transaction Initiator options.
#define ZX_BTI_PERM_READ            ((uint32_t)1u << 0)
#define ZX_BTI_PERM_WRITE           ((uint32_t)1u << 1)
#define ZX_BTI_PERM_EXECUTE         ((uint32_t)1u << 2)
#define ZX_BTI_COMPRESS             ((uint32_t)1u << 3)
#define ZX_BTI_CONTIGUOUS           ((uint32_t)1u << 4)

// Job options.
// These options can be passed to zx_job_set_critical().
#define ZX_JOB_CRITICAL_PROCESS_RETCODE_NONZERO     ((uint32_t)1u << 0)

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
#define ZX_OBJ_TYPE_PAGER           ((zx_obj_type_t)28u)
#define ZX_OBJ_TYPE_EXCEPTION       ((zx_obj_type_t)29u)
#define ZX_OBJ_TYPE_CLOCK           ((zx_obj_type_t)30u)
#define ZX_OBJ_TYPE_STREAM          ((zx_obj_type_t)31u)
#define ZX_OBJ_TYPE_MSI_ALLOCATION  ((zx_obj_type_t)32u)
#define ZX_OBJ_TYPE_MSI_INTERRUPT   ((zx_obj_type_t)33u)

// System ABI commits to having no more than 64 object types.
//
// See zx_info_process_handle_stats_t for an example of a binary interface that
// depends on having an upper bound for the number of object types.
#define ZX_OBJ_TYPE_UPPER_BOUND     ((zx_obj_type_t)64u)

typedef uint32_t zx_system_event_type_t;
#define ZX_SYSTEM_EVENT_OUT_OF_MEMORY               ((zx_system_event_type_t)1u)
#define ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL    ((zx_system_event_type_t)2u)
#define ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING     ((zx_system_event_type_t)3u)
#define ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL      ((zx_system_event_type_t)4u)

// Used in channel_read_etc.
typedef struct zx_handle_info {
    zx_handle_t handle;
    zx_obj_type_t type;
    zx_rights_t rights;
    uint32_t unused;
} zx_handle_info_t;

typedef uint32_t zx_handle_op_t;

#define ZX_HANDLE_OP_MOVE           ((zx_handle_op_t)0u)
#define ZX_HANDLE_OP_DUPLICATE      ((zx_handle_op_t)1u)

// Used in channel_write_etc.
typedef struct zx_handle_disposition {
    zx_handle_op_t operation;
    zx_handle_t handle;
    zx_obj_type_t type;
    zx_rights_t rights;
    zx_status_t result;
} zx_handle_disposition_t;

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

typedef struct zx_channel_call_etc_args {
    const void* wr_bytes;
    zx_handle_disposition_t* wr_handles;
    void *rd_bytes;
    zx_handle_info_t* rd_handles;
    uint32_t wr_num_bytes;
    uint32_t wr_num_handles;
    uint32_t rd_num_bytes;
    uint32_t rd_num_handles;
} zx_channel_call_etc_args_t;

// The ZX_VM_FLAG_* constants are to be deprecated in favor of the ZX_VM_*
// versions.
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

// CPU masks specifying sets of CPUs.
//
// We currently are limited to systems with 512 CPUs or less.
// TODO(fxbug.dev/7802): This must be manually kept in sync with zx_common.fidl.
// Eventually (some of) this file will be generated from //zircon/vdso.
#define ZX_CPU_SET_MAX_CPUS 512
#define ZX_CPU_SET_BITS_PER_WORD 64

typedef struct zx_cpu_set {
    // The |N|'th CPU is considered in the CPU set if the bit:
    //
    //   cpu_mask[N / ZX_CPU_SET_BITS_PER_WORD]
    //       & (1 << (N % ZX_CPU_SET_BITS_PER_WORD))
    //
    // is set.
    uint64_t mask[ZX_CPU_SET_MAX_CPUS / ZX_CPU_SET_BITS_PER_WORD];
} zx_cpu_set_t;

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
typedef int zx_futex_storage_t;

__END_CDECLS

#endif // SYSROOT_ZIRCON_TYPES_H_
