// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Valid topics for mx_object_get_info.
typedef enum {
    MX_INFO_NONE                       = 0,
    MX_INFO_HANDLE_VALID               = 1,
    MX_INFO_HANDLE_BASIC               = 2,  // mx_info_handle_basic_t[1]
    MX_INFO_PROCESS                    = 3,  // mx_info_process_t[1]
    MX_INFO_PROCESS_THREADS            = 4,  // mx_koid_t[n]
    MX_INFO_RESOURCE_CHILDREN          = 5,  // mx_rrec_t[n]
    MX_INFO_RESOURCE_RECORDS           = 6,  // mx_rrec_t[n]
    MX_INFO_VMAR                       = 7,  // mx_info_vmar_t
    MX_INFO_JOB_CHILDREN               = 8,  // mx_koid_t[n]
    MX_INFO_JOB_PROCESSES              = 9,  // mx_koid_t[n]
    MX_INFO_THREAD                     = 10, // mx_info_thread_t[1]
    MX_INFO_THREAD_EXCEPTION_REPORT    = 11, // mx_exception_report_t[1]
    MX_INFO_TASK_STATS                 = 12, // mx_info_task_stats_t[1]
    MX_INFO_PROCESS_MAPS               = 13, // mx_info_maps_t[n]
    MX_INFO_THREAD_STATS               = 14, // mx_info_thread_stats_t[1]
    MX_INFO_LAST
} mx_object_info_topic_t;

typedef enum {
    MX_OBJ_TYPE_NONE                = 0,
    MX_OBJ_TYPE_PROCESS             = 1,
    MX_OBJ_TYPE_THREAD              = 2,
    MX_OBJ_TYPE_VMEM                = 3,
    MX_OBJ_TYPE_CHANNEL             = 4,
    MX_OBJ_TYPE_EVENT               = 5,
    MX_OBJ_TYPE_IOPORT              = 6,
    MX_OBJ_TYPE_INTERRUPT           = 9,
    MX_OBJ_TYPE_IOMAP               = 10,
    MX_OBJ_TYPE_PCI_DEVICE          = 11,
    MX_OBJ_TYPE_LOG                 = 12,
    MX_OBJ_TYPE_WAIT_SET            = 13,
    MX_OBJ_TYPE_SOCKET              = 14,
    MX_OBJ_TYPE_RESOURCE            = 15,
    MX_OBJ_TYPE_EVENT_PAIR          = 16,
    MX_OBJ_TYPE_JOB                 = 17,
    MX_OBJ_TYPE_VMAR                = 18,
    MX_OBJ_TYPE_FIFO                = 19,
    MX_OBJ_TYPE_IOPORT2             = 20,
    MX_OBJ_TYPE_HYPERVISOR          = 21,
    MX_OBJ_TYPE_GUEST               = 22,
    MX_OBJ_TYPE_LAST
} mx_obj_type_t;

typedef enum {
    MX_OBJ_PROP_NONE            = 0,
    MX_OBJ_PROP_WAITABLE        = 1,
} mx_obj_props_t;

typedef struct mx_info_handle_basic {
    // The unique id assigned by kernel to the object referenced by the
    // handle.
    mx_koid_t koid;

    // The immutable rights assigned to the handle. Two handles that
    // have the same koid and the same rights are equivalent and
    // interchangeable.
    mx_rights_t rights;

    // The object type: channel, event, socket, etc.
    uint32_t type;                // mx_obj_type_t;

    // The koid of the logical counterpart or parent object of the
    // object referenced by the handle. Otherwise this value is zero.
    mx_koid_t related_koid;

    // Set to MX_OBJ_PROP_WAITABLE if the object referenced by the
    // handle can be waited on; zero otherwise.
    uint32_t props;               // mx_obj_props_t;
} mx_info_handle_basic_t;

typedef struct mx_info_process {
    // The process's return code; only valid if |exited| is true.
    // Guaranteed to be non-zero if the process was killed by |mx_task_kill|.
    int return_code;

    // True if the process has ever left the initial creation state,
    // even if it has exited as well.
    bool started;

    // If true, the process has exited and |return_code| is valid.
    bool exited;

    // True if a debugger is attached to the process.
    bool debugger_attached;
} mx_info_process_t;

typedef struct mx_info_thread {
    // One of MX_THREAD_STATE_* values.
    uint32_t state;

    // If nonzero, the thread has gotten an exception and is waiting for
    // the exception to be handled by the specified port.
    // The value is one of MX_EXCEPTION_PORT_TYPE_*.
    uint32_t wait_exception_port_type;
} mx_info_thread_t;

typedef struct mx_info_thread_stats {
    // Total accumulated running time of the thread.
    mx_time_t total_runtime;
} mx_info_thread_stats_t;

// Statistics about resources (e.g., memory) used by a task. Can be relatively
// expensive to gather.
typedef struct mx_info_task_stats {
    // The total size of mapped memory ranges in the task.
    // Not all will be backed by physical memory.
    size_t mem_mapped_bytes;

    // The amount of mapped address space backed by physical memory.
    // Will be no larger than mem_mapped_bytes.
    // Some of the pages may be double-mapped (and thus double-counted),
    // or may be shared with other tasks.
    // TODO(dbort): Remove mem_committed_bytes, which is equal to
    // mem_private_bytes + mem_shared_bytes.
    size_t mem_committed_bytes;

    // For the fields below, a byte is considered committed if it's backed by
    // physical memory. Some of the memory may be double-mapped, and thus
    // double-counted.

    // Committed memory that is only mapped into this task.
    size_t mem_private_bytes;

    // Committed memory that is mapped into this and at least one other task.
    size_t mem_shared_bytes;

    // A number that estimates the fraction of mem_shared_bytes that this
    // task is responsible for keeping alive.
    //
    // An estimate of:
    //   For each shared, committed byte:
    //   mem_scaled_shared_bytes += 1 / (number of tasks mapping this byte)
    //
    // This number is strictly smaller than mem_shared_bytes.
    size_t mem_scaled_shared_bytes;
} mx_info_task_stats_t;

typedef struct mx_info_vmar {
    // Base address of the region.
    uintptr_t base;

    // Length of the region, in bytes.
    size_t len;
} mx_info_vmar_t;


// Types and values used by MX_INFO_PROCESS_MAPS.

// Describes a VM mapping.
typedef struct mx_info_maps_mapping {
    // MMU flags for the mapping.
    // Bitwise OR of MX_VM_FLAG_PERM_{READ,WRITE,EXECUTE} values.
    uint32_t mmu_flags;
    // The number of PAGE_SIZE pages in the mapped region of the VMO
    // that are backed by physical memory.
    size_t committed_pages;
} mx_info_maps_mapping_t;

// Types of entries represented by mx_info_maps_t.
// Can't use mx_obj_type_t because not all of these are
// user-visible kernel object types.
typedef enum mx_info_maps_type {
    MX_INFO_MAPS_TYPE_NONE    = 0,
    MX_INFO_MAPS_TYPE_ASPACE  = 1,
    MX_INFO_MAPS_TYPE_VMAR    = 2,
    MX_INFO_MAPS_TYPE_MAPPING = 3,
    MX_INFO_MAPS_TYPE_LAST
} mx_info_maps_type_t;

// Describes a node in the aspace/vmar/mapping hierarchy for a user process.
typedef struct mx_info_maps {
    // Name if available; empty string otherwise.
    char name[MX_MAX_NAME_LEN];
    // Base address.
    mx_vaddr_t base;
    // Size in bytes.
    size_t size;

    // The depth of this node in the tree.
    // Can be used for indentation, or to rebuild the tree from an array
    // of mx_info_maps_t entries, which will be in depth-first pre-order.
    size_t depth;
    // The type of this entry; indicates which union entry is valid.
    uint32_t type; // mx_info_maps_type_t
    union {
        mx_info_maps_mapping_t mapping;
        // No additional fields for other types.
    } u;
} mx_info_maps_t;


// Object properties.

// Argument is a uint32_t.
#define MX_PROP_NUM_STATE_KINDS             2u
// Argument is a char[MX_MAX_NAME_LEN].
#define MX_PROP_NAME                        3u

#if __x86_64__
// Argument is a uintptr_t.
#define MX_PROP_REGISTER_FS                 4u
#endif

// Argument is the value of ld.so's _dl_debug_addr, a uintptr_t.
#define MX_PROP_PROCESS_DEBUG_ADDR          5u

// Argument is the base address of the vDSO mapping (or zero), a uintptr_t.
#define MX_PROP_PROCESS_VDSO_BASE_ADDRESS   6u

// Argument is the number of descendant generations that a job is allowed to
// have, as a uint32_t.
//
// A job has a MAX_HEIGHT value equal to one less than its parent's MAX_HEIGHT
// value.
//
// A job with MAX_HEIGHT equal to zero may not have any child jobs, and calling
// mx_job_create() on such a job will fail with ERR_OUT_OF_RANGE. MAX_HEIGHT
// does not affect the creation of processes.
#define MX_PROP_JOB_MAX_HEIGHT              7u

// Values for mx_info_thread_t.state.
#define MX_THREAD_STATE_NEW                 0u
#define MX_THREAD_STATE_RUNNING             1u
#define MX_THREAD_STATE_SUSPENDED           2u
#define MX_THREAD_STATE_BLOCKED             3u
#define MX_THREAD_STATE_DYING               4u
#define MX_THREAD_STATE_DEAD                5u

__END_CDECLS
