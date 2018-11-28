// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSCALLS_OBJECT_H_
#define ZIRCON_SYSCALLS_OBJECT_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// Valid topics for zx_object_get_info.
typedef uint32_t zx_object_info_topic_t;
#define ZX_INFO_NONE                    ((zx_object_info_topic_t)  0u)
#define ZX_INFO_HANDLE_VALID            ((zx_object_info_topic_t)  1u)
#define ZX_INFO_HANDLE_BASIC            ((zx_object_info_topic_t)  2u) // zx_info_handle_basic_t[1]
#define ZX_INFO_PROCESS                 ((zx_object_info_topic_t)  3u) // zx_info_process_t[1]
#define ZX_INFO_PROCESS_THREADS         ((zx_object_info_topic_t)  4u) // zx_koid_t[n]
#define ZX_INFO_VMAR                    ((zx_object_info_topic_t)  7u) // zx_info_vmar_t[1]
#define ZX_INFO_JOB_CHILDREN            ((zx_object_info_topic_t)  8u) // zx_koid_t[n]
#define ZX_INFO_JOB_PROCESSES           ((zx_object_info_topic_t)  9u) // zx_koid_t[n]
#define ZX_INFO_THREAD                  ((zx_object_info_topic_t) 10u) // zx_info_thread_t[1]
#define ZX_INFO_THREAD_EXCEPTION_REPORT ((zx_object_info_topic_t) 11u) // zx_exception_report_t[1]
#define ZX_INFO_TASK_STATS              ((zx_object_info_topic_t) 12u) // zx_info_task_stats_t[1]
#define ZX_INFO_PROCESS_MAPS            ((zx_object_info_topic_t) 13u) // zx_info_maps_t[n]
#define ZX_INFO_PROCESS_VMOS            ((zx_object_info_topic_t) 14u) // zx_info_vmo_t[n]
#define ZX_INFO_THREAD_STATS            ((zx_object_info_topic_t) 15u) // zx_info_thread_stats_t[1]
#define ZX_INFO_CPU_STATS               ((zx_object_info_topic_t) 16u) // zx_info_cpu_stats_t[n]
#define ZX_INFO_KMEM_STATS              ((zx_object_info_topic_t) 17u) // zx_info_kmem_stats_t[1]
#define ZX_INFO_RESOURCE                ((zx_object_info_topic_t) 18u) // zx_info_resource_t[1]
#define ZX_INFO_HANDLE_COUNT            ((zx_object_info_topic_t) 19u) // zx_info_handle_count_t[1]
#define ZX_INFO_BTI                     ((zx_object_info_topic_t) 20u) // zx_info_bti_t[1]
#define ZX_INFO_PROCESS_HANDLE_STATS    ((zx_object_info_topic_t) 21u) // zx_info_process_handle_stats_t[1]
#define ZX_INFO_SOCKET                  ((zx_object_info_topic_t) 22u) // zx_info_socket_t[1]
#define ZX_INFO_VMO                     ((zx_object_info_topic_t) 23u) // zx_info_vmo_t[1]

typedef uint32_t zx_obj_props_t;
#define ZX_OBJ_PROP_NONE                ((zx_obj_props_t)0u)
#define ZX_OBJ_PROP_WAITABLE            ((zx_obj_props_t)1u)

typedef struct zx_info_handle_basic {
    // The unique id assigned by kernel to the object referenced by the
    // handle.
    zx_koid_t koid;

    // The immutable rights assigned to the handle. Two handles that
    // have the same koid and the same rights are equivalent and
    // interchangeable.
    zx_rights_t rights;

    // The object type: channel, event, socket, etc.
    zx_obj_type_t type;

    // If the object referenced by the handle is related to another (such
    // as the other end of a channel, or the parent of a job) then
    // |related_koid| is the koid of that object, otherwise it is zero.
    // This relationship is immutable: an object's |related_koid| does
    // not change even if the related object no longer exists.
    zx_koid_t related_koid;

    // Set to ZX_OBJ_PROP_WAITABLE if the object referenced by the
    // handle can be waited on; zero otherwise.
    zx_obj_props_t props;
} zx_info_handle_basic_t;

typedef struct zx_info_handle_count {
    // The number of outstanding handles to a kernel object.
    uint32_t handle_count;
} zx_info_handle_count_t;

typedef struct zx_info_process_handle_stats {
    // The number of outstanding handles to kernel objects of each type.
    uint32_t handle_count[64];
} zx_info_process_handle_stats_t;

typedef struct zx_info_process {
    // The process's return code; only valid if |exited| is true.
    // Guaranteed to be non-zero if the process was killed by |zx_task_kill|.
    int64_t return_code;

    // True if the process has ever left the initial creation state,
    // even if it has exited as well.
    bool started;

    // If true, the process has exited and |return_code| is valid.
    bool exited;

    // True if a debugger is attached to the process.
    bool debugger_attached;
} zx_info_process_t;

typedef uint32_t zx_thread_state_t;

typedef struct zx_info_thread {
    // One of ZX_THREAD_STATE_* values.
    zx_thread_state_t state;

    // If |state| is ZX_THREAD_STATE_BLOCKED_EXCEPTION, the thread has gotten
    // an exception and is waiting for the exception to be handled by the
    // specified port.
    // The value is one of ZX_EXCEPTION_PORT_TYPE_*.
    uint32_t wait_exception_port_type;
} zx_info_thread_t;

typedef struct zx_info_thread_stats {
    // Total accumulated running time of the thread.
    zx_duration_t total_runtime;
} zx_info_thread_stats_t;

// Statistics about resources (e.g., memory) used by a task. Can be relatively
// expensive to gather.
typedef struct zx_info_task_stats {
    // The total size of mapped memory ranges in the task.
    // Not all will be backed by physical memory.
    size_t mem_mapped_bytes;

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
} zx_info_task_stats_t;

typedef struct zx_info_vmar {
    // Base address of the region.
    uintptr_t base;

    // Length of the region, in bytes.
    size_t len;
} zx_info_vmar_t;

typedef struct zx_info_bti {
    // zx_bti_pin will always be able to return addresses that are contiguous for at
    // least this many bytes.  E.g. if this returns 1MB, then a call to
    // zx_bti_pin() with a size of 2MB will return at most two physically-contiguous runs.
    // If the size were 2.5MB, it will return at most three physically-contiguous runs.
    uint64_t minimum_contiguity;

    // The number of bytes in the device's address space (UINT64_MAX if 2^64).
    uint64_t aspace_size;
} zx_info_bti_t;

typedef struct zx_info_socket {
    // The options passed to zx_socket_create().
    uint32_t options;

    // The maximum size of the receive buffer of a socket, in bytes.
    //
    // The receive buffer may become full at a capacity less than the maximum
    // due to overhead.
    size_t rx_buf_max;

    // The size of the receive buffer of a socket, in bytes.
    size_t rx_buf_size;

    // The amount of data, in bytes, that is available for reading in a single
    // zx_socket_read call.
    //
    // For stream sockets, this value will match |rx_buf_size|. For datagram
    // sockets, this value will be the size of the next datagram in the receive
    // buffer.
    size_t rx_buf_available;

    // The maximum size of the transmit buffer of a socket, in bytes.
    //
    // The transmit buffer may become full at a capacity less than the maximum
    // due to overhead.
    //
    // Will be zero if the peer endpoint is closed.
    size_t tx_buf_max;

    // The size of the transmit buffer of a socket, in bytes.
    //
    // Will be zero if the peer endpoint is closed.
    size_t tx_buf_size;
} zx_info_socket_t;

// Types and values used by ZX_INFO_PROCESS_MAPS.

// Describes a VM mapping.
typedef struct zx_info_maps_mapping {
    // MMU flags for the mapping.
    // Bitwise OR of ZX_VM_PERM_{READ,WRITE,EXECUTE} values.
    zx_vm_option_t mmu_flags;
    // koid of the mapped VMO.
    zx_koid_t vmo_koid;
    // Offset into the above VMO.
    uint64_t vmo_offset;
    // The number of PAGE_SIZE pages in the mapped region of the VMO
    // that are backed by physical memory.
    size_t committed_pages;
} zx_info_maps_mapping_t;

// Types of entries represented by zx_info_maps_t.
// Can't use zx_obj_type_t because not all of these are
// user-visible kernel object types.
typedef uint32_t zx_info_maps_type_t;
#define ZX_INFO_MAPS_TYPE_NONE    ((zx_info_maps_type_t) 0u)
#define ZX_INFO_MAPS_TYPE_ASPACE  ((zx_info_maps_type_t) 1u)
#define ZX_INFO_MAPS_TYPE_VMAR    ((zx_info_maps_type_t) 2u)
#define ZX_INFO_MAPS_TYPE_MAPPING ((zx_info_maps_type_t) 3u)

// Describes a node in the aspace/vmar/mapping hierarchy for a user process.
typedef struct zx_info_maps {
    // Name if available; empty string otherwise.
    char name[ZX_MAX_NAME_LEN];
    // Base address.
    zx_vaddr_t base;
    // Size in bytes.
    size_t size;

    // The depth of this node in the tree.
    // Can be used for indentation, or to rebuild the tree from an array
    // of zx_info_maps_t entries, which will be in depth-first pre-order.
    size_t depth;
    // The type of this entry; indicates which union entry is valid.
    zx_info_maps_type_t type;
    union {
        zx_info_maps_mapping_t mapping;
        // No additional fields for other types.
    } u;
} zx_info_maps_t;


// Values and types used by ZX_INFO_PROCESS_VMOS.

// The VMO is backed by RAM, consuming memory.
// Mutually exclusive with ZX_INFO_VMO_TYPE_PHYSICAL.
// See ZX_INFO_VMO_TYPE(flags)
#define ZX_INFO_VMO_TYPE_PAGED              (1u<<0)

// The VMO points to a physical address range, and does not consume memory.
// Typically used to access memory-mapped hardware.
// Mutually exclusive with ZX_INFO_VMO_TYPE_PAGED.
// See ZX_INFO_VMO_TYPE(flags)
#define ZX_INFO_VMO_TYPE_PHYSICAL           (0u<<0)

// Returns a VMO's type based on its flags, allowing for checks like
// if (ZX_INFO_VMO_TYPE(f) == ZX_INFO_VMO_TYPE_PAGED)
#define ZX_INFO_VMO_TYPE(flags)             ((flags) & (1u<<0))

// The VMO is a clone, and is a copy-on-write clone.
#define ZX_INFO_VMO_IS_COW_CLONE            (1u<<2)

// When reading a list of VMOs pointed to by a process, indicates that the
// process has a handle to the VMO, which isn't necessarily mapped.
#define ZX_INFO_VMO_VIA_HANDLE              (1u<<3)

// When reading a list of VMOs pointed to by a process, indicates that the
// process maps the VMO into a VMAR, but doesn't necessarily have a handle to
// the VMO.
#define ZX_INFO_VMO_VIA_MAPPING             (1u<<4)

// Describes a VMO. For mapping information, see |zx_info_maps_t|.
typedef struct zx_info_vmo {
    // The koid of this VMO.
    zx_koid_t koid;

    // The name of this VMO.
    char name[ZX_MAX_NAME_LEN];

    // The size of this VMO; i.e., the amount of virtual address space it
    // would consume if mapped.
    uint64_t size_bytes;

    // If this VMO is a clone, the koid of its parent. Otherwise, zero.
    // See |flags| for the type of clone.
    zx_koid_t parent_koid;

    // The number of clones of this VMO, if any.
    size_t num_children;

    // The number of times this VMO is currently mapped into VMARs.
    // Note that the same process will often map the same VMO twice,
    // and both mappings will be counted here. (I.e., this is not a count
    // of the number of processes that map this VMO; see share_count.)
    size_t num_mappings;

    // An estimate of the number of unique address spaces that
    // this VMO is mapped into. Every process has its own address space,
    // and so does the kernel.
    size_t share_count;

    // Bitwise OR of ZX_INFO_VMO_* values.
    uint32_t flags;

    // If |ZX_INFO_VMO_TYPE(flags) == ZX_INFO_VMO_TYPE_PAGED|, the amount of
    // memory currently allocated to this VMO; i.e., the amount of physical
    // memory it consumes. Undefined otherwise.
    uint64_t committed_bytes;

    // If |flags & ZX_INFO_VMO_VIA_HANDLE|, the handle rights.
    // Undefined otherwise.
    zx_rights_t handle_rights;

    // VMO creation options. This is a bitmask of
    // kResizable    = (1u << 0);
    // kContiguous   = (1u << 1);
    uint32_t create_options;

    // VMO mapping cache policy. One of ZX_CACHE_POLICY_*
    uint32_t cache_policy;
} zx_info_vmo_t;

// kernel statistics per cpu
// TODO(cpu), expose the deprecated stats via a new syscall.
typedef struct zx_info_cpu_stats {
    uint32_t cpu_number;
    uint32_t flags;

    zx_duration_t idle_time;

    // kernel scheduler counters
    uint64_t reschedules;
    uint64_t context_switches;
    uint64_t irq_preempts;
    uint64_t preempts;
    uint64_t yields;

    // cpu level interrupts and exceptions
    uint64_t ints;          // hardware interrupts, minus timer interrupts or inter-processor interrupts
    uint64_t timer_ints;    // timer interrupts
    uint64_t timers;        // timer callbacks
    uint64_t page_faults;   // (deprecated, returns 0) page faults
    uint64_t exceptions;    // (deprecated, returns 0) exceptions such as undefined opcode
    uint64_t syscalls;

    // inter-processor interrupts
    uint64_t reschedule_ipis;
    uint64_t generic_ipis;
} zx_info_cpu_stats_t;

// Information about kernel memory usage.
// Can be expensive to gather.
typedef struct zx_info_kmem_stats {
    // The total amount of physical memory available to the system.
    uint64_t total_bytes;

    // The amount of unallocated memory.
    uint64_t free_bytes;

    // The amount of memory reserved by and mapped into the kernel for reasons
    // not covered by other fields in this struct. Typically for readonly data
    // like the ram disk and kernel image, and for early-boot dynamic memory.
    uint64_t wired_bytes;

    // The amount of memory allocated to the kernel heap.
    uint64_t total_heap_bytes;

    // The portion of |total_heap_bytes| that is not in use.
    uint64_t free_heap_bytes;

    // The amount of memory committed to VMOs, both kernel and user.
    // A superset of all userspace memory.
    // Does not include certain VMOs that fall under |wired_bytes|.
    //
    // TODO(dbort): Break this into at least two pieces: userspace VMOs that
    // have koids, and kernel VMOs that don't. Or maybe look at VMOs
    // mapped into the kernel aspace vs. everything else.
    uint64_t vmo_bytes;

    // The amount of memory used for architecture-specific MMU metadata
    // like page tables.
    uint64_t mmu_overhead_bytes;

    // The amount of memory in use by IPC.
    uint64_t ipc_bytes;

    // Non-free memory that isn't accounted for in any other field.
    uint64_t other_bytes;
} zx_info_kmem_stats_t;

typedef struct zx_info_resource {
    // The resource kind; resource object kinds are detailed in the resource.md
    uint32_t kind;
    // Resource's creation flags
    uint32_t flags;
    // Resource's base value (inclusive)
    uint64_t base;
    // Resource's length value
    size_t size;
    char name[ZX_MAX_NAME_LEN];
} zx_info_resource_t;

#define ZX_INFO_CPU_STATS_FLAG_ONLINE       (1u<<0)

// Object properties.

// Argument is a char[ZX_MAX_NAME_LEN].
#define ZX_PROP_NAME                        ((uint32_t) 3u)

#if __x86_64__
// Argument is a uintptr_t.
#define ZX_PROP_REGISTER_GS                 ((uint32_t) 2u)
#define ZX_PROP_REGISTER_FS                 ((uint32_t) 4u)
#endif

// Argument is the value of ld.so's _dl_debug_addr, a uintptr_t. If the
// property is set to the magic value of ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET
// on process startup, ld.so will trigger a debug breakpoint immediately after
// setting the property to the correct value.
#define ZX_PROP_PROCESS_DEBUG_ADDR          ((uint32_t) 5u)
#define ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET  ((uintptr_t) 1u)

// Argument is the base address of the vDSO mapping (or zero), a uintptr_t.
#define ZX_PROP_PROCESS_VDSO_BASE_ADDRESS   ((uint32_t) 6u)

// Argument is a size_t.
#define ZX_PROP_SOCKET_RX_THRESHOLD         12u
#define ZX_PROP_SOCKET_TX_THRESHOLD         13u

// Terminate this job if the system is low on memory.
#define ZX_PROP_JOB_KILL_ON_OOM             15u

// Basic thread states, in zx_info_thread_t.state.
#define ZX_THREAD_STATE_NEW                 ((zx_thread_state_t) 0x0000u)
#define ZX_THREAD_STATE_RUNNING             ((zx_thread_state_t) 0x0001u)
#define ZX_THREAD_STATE_SUSPENDED           ((zx_thread_state_t) 0x0002u)
// ZX_THREAD_STATE_BLOCKED is never returned by itself.
// It is always returned with a more precise reason.
// See ZX_THREAD_STATE_BLOCKED_* below.
#define ZX_THREAD_STATE_BLOCKED             ((zx_thread_state_t) 0x0003u)
#define ZX_THREAD_STATE_DYING               ((zx_thread_state_t) 0x0004u)
#define ZX_THREAD_STATE_DEAD                ((zx_thread_state_t) 0x0005u)

// More precise thread states.
#define ZX_THREAD_STATE_BLOCKED_EXCEPTION   ((zx_thread_state_t) 0x0103u)
#define ZX_THREAD_STATE_BLOCKED_SLEEPING    ((zx_thread_state_t) 0x0203u)
#define ZX_THREAD_STATE_BLOCKED_FUTEX       ((zx_thread_state_t) 0x0303u)
#define ZX_THREAD_STATE_BLOCKED_PORT        ((zx_thread_state_t) 0x0403u)
#define ZX_THREAD_STATE_BLOCKED_CHANNEL     ((zx_thread_state_t) 0x0503u)
#define ZX_THREAD_STATE_BLOCKED_WAIT_ONE    ((zx_thread_state_t) 0x0603u)
#define ZX_THREAD_STATE_BLOCKED_WAIT_MANY   ((zx_thread_state_t) 0x0703u)
#define ZX_THREAD_STATE_BLOCKED_INTERRUPT   ((zx_thread_state_t) 0x0803u)

// Reduce possibly-more-precise state to a basic state.
// Useful if, for example, you want to check for BLOCKED on anything.
#define ZX_THREAD_STATE_BASIC(n) ((n) & 0xff)

__END_CDECLS

#endif // ZIRCON_SYSCALLS_OBJECT_H_
