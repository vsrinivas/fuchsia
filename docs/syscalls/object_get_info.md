# zx_object_get_info

## NAME

object_get_info - query information about an object

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

zx_status_t zx_object_get_info(zx_handle_t handle, uint32_t topic,
                               void* buffer, size_t buffer_size,
                               size_t* actual, size_t* avail);
```

## DESCRIPTION

**object_get_info()** requests information about the provided handle (or the
object the handle refers to). The *topic* parameter indicates what specific
information is desired.

*buffer* is a pointer to a buffer of size *buffer_size* to return the
information.

*actual* is an optional pointer to return the number of records that were
written to buffer.

*avail* is an optional pointer to return the number of records that are
available to read.

If the buffer is insufficiently large, *avail* will be larger than *actual*.

[TOC]

## TOPICS

### ZX_INFO_HANDLE_VALID

*handle* type: **Any**

*buffer* type: **n/a**

Returns **ZX_OK** if *handle* is valid, or **ZX_ERR_BAD_HANDLE** otherwise. No
records are returned and *buffer* may be NULL.

### ZX_INFO_HANDLE_BASIC

*handle* type: **Any**

*buffer* type: **zx_info_handle_basic_t[1]**

```
typedef struct zx_info_handle_basic {
    // The unique id assigned by kernel to the object referenced by the
    // handle.
    zx_koid_t koid;

    // The immutable rights assigned to the handle. Two handles that
    // have the same koid and the same rights are equivalent and
    // interchangeable.
    zx_rights_t rights;

    // The object type: channel, event, socket, etc.
    uint32_t type;                // zx_obj_type_t;

    // If the object referenced by the handle is related to another (such
    // as the the other end of a channel, or the parent of a job) then
    // |related_koid| is the koid of that object, otherwise it is zero.
    // This relationship is immutable: an object's |related_koid| does
    // not change even if the related object no longer exists.
    zx_koid_t related_koid;

    // Set to ZX_OBJ_PROP_WAITABLE if the object referenced by the
    // handle can be waited on; zero otherwise.
    uint32_t props;               // zx_obj_props_t;
} zx_info_handle_basic_t;
```

### ZX_INFO_HANDLE_COUNT

*handle* type: **Any**

*buffer* type: **zx_info_handle_count_t[1]**

```
typedef struct zx_info_handle_count {
    // The number of outstanding handles to a kernel object.
    uint32_t handle_count;
} zx_info_handle_count_t;
```

The *handle_count* is only meaningful if the number is equal to the number
of handles to the object controlled by the caller process. For example,
if the caller has one handle and the *handle_count* is equal to 2 it
means that another process has a reference to this object which can be
duplicated at any time.

### ZX_INFO_PROCESS_HANDLE_STATS

*handle* type: **Process**

*buffer* type: **zx_info_process_handle_stats_t[1]**

```
typedef struct zx_info_process_handle_stats {
    // The number of outstanding handles to kernel objects of each type.
    uint32_t handle_count[ZX_OBJ_TYPE_LAST];
} zx_info_process_handle_stats_t;
```

### ZX_INFO_PROCESS

*handle* type: **Process**

*buffer* type: **zx_info_process_t[1]**

```
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
```

### ZX_INFO_PROCESS_THREADS

*handle* type: **Process**

*buffer* type: **zx_koid_t[n]**

Returns an array of *zx_koid_t*, one for each running thread in the Process at
that moment in time.

N.B. Getting the list of threads is inherently racy.
This can be somewhat mitigated by first suspending all the threads,
but note that an external thread can create new threads.
*actual* will contain the number of threads returned in *buffer*.
*avail* will contain the total number of threads of the process at
the time the list of threads was obtained, it could be larger than *actual*.

### ZX_INFO_THREAD

*handle* type: **Thread**

*buffer* type: **zx_info_thread_t[1]**

```
typedef struct zx_info_thread {
    // One of ZX_THREAD_STATE_* values.
    uint32_t state;

    // If |state| is ZX_THREAD_STATE_BLOCKED_EXCEPTION, the thread has gotten
    // an exception and is waiting for the exception to be handled by the
    // specified port.
    // The value is one of ZX_EXCEPTION_PORT_TYPE_*.
    uint32_t wait_exception_port_type;
} zx_info_thread_t;
```

The values in this struct are mainly for informational and debugging
purposes at the moment.

The **ZX_THREAD_STATE_\*** values are defined by

```
#include <zircon/syscalls/object.h>
```

*   *ZX_THREAD_STATE_NEW*: The thread has been created but it has not started running yet.
*   *ZX_THREAD_STATE_RUNNING*: The thread is running user code normally.
*   *ZX_THREAD_STATE_SUSPENDED*: Stopped due to [zx_task_suspend](task_suspend.md).
*   *ZX_THREAD_STATE_BLOCKED*: In a syscall or handling an exception.
    This value is never returned by itself.
	See **ZX_THREAD_STATE_BLOCKED_\*** below.
*   *ZX_THREAD_STATE_DYING*: The thread is in the process of being terminated,
    but it has not been stopped yet.
*   *ZX_THREAD_STATE_DEAD*: The thread has stopped running.

When a thread is stopped inside a blocking syscall, or stopped in an
exception, the value returned in **state** is one of the following:

*   *ZX_THREAD_STATE_BLOCKED_EXCEPTION*: The thread is stopped in an exception.
*   *ZX_THREAD_STATE_BLOCKED_SLEEPING*: The thread is stopped in [zx_nanosleep](nanosleep.md).
*   *ZX_THREAD_STATE_BLOCKED_FUTEX*: The thread is stopped in [zx_futex_wait](futex_wait.md).
*   *ZX_THREAD_STATE_BLOCKED_PORT*: The thread is stopped in [zx_port_wait](port_wait.md).
*   *ZX_THREAD_STATE_BLOCKED_CHANNEL*: The thread is stopped in [zx_channel_call](channel_call.md).
*   *ZX_THREAD_STATE_BLOCKED_WAIT_ONE*: The thread is stopped in [zx_object_wait_one](object_wait_one.md).
*   *ZX_THREAD_STATE_BLOCKED_WAIT_MANY*: The thread is stopped in [zx_object_wait_many](object_wait_many.md).
*   *ZX_THREAD_STATE_BLOCKED_INTERRUPT*: The thread is stopped in [zx_interrupt_wait](interrupt_wait.md).

The **ZX_EXCEPTION_PORT_TYPE_\*** values are defined by

```
#include <zircon/syscalls/exception.h>
```

*   *ZX_EXCEPTION_PORT_TYPE_NONE*
*   *ZX_EXCEPTION_PORT_TYPE_DEBUGGER*
*   *ZX_EXCEPTION_PORT_TYPE_THREAD*
*   *ZX_EXCEPTION_PORT_TYPE_PROCESS*
*   *ZX_EXCEPTION_PORT_TYPE_JOB*

### ZX_INFO_THREAD_EXCEPTION_REPORT

*handle* type: **Thread**

*buffer* type: **zx_exception_report_t[1]**

```
#include <zircon/syscalls/exception.h>
```

If the thread is currently in an exception and is waiting for an exception
response, then this returns the exception report as a single
*zx_exception_report_t*, with status ZX_OK.

Returns **ZX_ERR_BAD_STATE** if the thread is not in an exception and waiting for
an exception response.

### ZX_INFO_THREAD_STATS

*handle* type: **Thread**

*buffer* type: **zx_info_thread_stats[1]**

```
typedef struct zx_info_thread_stats {
    // Total accumulated running time of the thread.
    zx_duration_t total_runtime;
} zx_info_thread_stats_t;
```


### ZX_INFO_CPU_STATS

Note: many values of this topic are being retired in favor of a different mechanism.

*handle* type: **Resource** (Specifically, the root resource)

*buffer* type: **zx_info_cpu_stats_t[1]**

```
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
    uint64_t ints;          // hardware interrupts, minus timer interrupts
                            // inter-processor interrupts
    uint64_t timer_ints;    // timer interrupts
    uint64_t timers;        // timer callbacks
    uint64_t page_faults;   // (deprecated, returns 0)
    uint64_t exceptions;    // (deprecated, returns 0)
    uint64_t syscalls;

    // inter-processor interrupts
    uint64_t reschedule_ipis;
    uint64_t generic_ipis;
} zx_info_cpu_stats_t;
```


### ZX_INFO_VMAR

*handle* type: **VM Address Region**

*buffer* type: **zx_info_vmar_t[1]**

```
typedef struct zx_info_vmar {
    // Base address of the region.
    uintptr_t base;

    // Length of the region, in bytes.
    size_t len;
} zx_info_vmar_t;
```

### ZX_INFO_JOB_CHILDREN

*handle* type: **Job**

*buffer* type: **zx_koid_t[n]**

Returns an array of *zx_koid_t*, one for each direct child Job of the provided
Job handle.

### ZX_INFO_JOB_PROCESSES

*handle* type: **Job**

*buffer* type: **zx_koid_t[n]**

Returns an array of *zx_koid_t*, one for each direct child Process of the
provided Job handle.

### ZX_INFO_TASK_STATS

*handle* type: **Process**

*buffer* type: **zx_info_task_stats_t[1]**

Returns statistics about resources (e.g., memory) used by a task.

```
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
```

Additional errors:

*   **ZX_ERR_BAD_STATE**: If the target process is not currently running.

### ZX_INFO_PROCESS_MAPS

*handle* type: **Process** other than your own, with **ZX_RIGHT_READ**

*buffer* type: **zx_info_maps_t[n]**

The *zx_info_maps_t* array is a depth-first pre-order walk of the target
process's Aspace/VMAR/Mapping tree.

```
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
    uint32_t type; // zx_info_maps_type_t
    union {
        zx_info_maps_mapping_t mapping;
        // No additional fields for other types.
    } u;
} zx_info_maps_t;
```

The *depth* field of each entry describes its relationship to the nodes that
come before it. Depth 0 is the root Aspace, depth 1 is the root VMAR, and all
other entries have depth 2 or greater.

To get a full picture of how a process uses its VMOs and how a VMO is used
by various processes, you may need to combine this information with
ZX_INFO_PROCESS_VMOS.

See the `vmaps` command-line tool for an example user of this topic, and to dump
the maps of arbitrary processes by koid.

Additional errors:

*   **ZX_ERR_ACCESS_DENIED**: If the appropriate rights are missing, or if a
    process attempts to call this on a handle to itself. It's not safe to
    examine yourself: *buffer* will live inside the Aspace being examined, and
    the kernel can't safely fault in pages of the buffer while walking the
    Aspace.
*   **ZX_ERR_BAD_STATE**: If the target process is not currently running, or if
    its address space has been destroyed.

### ZX_INFO_PROCESS_VMOS

*handle* type: **Process** other than your own, with **ZX_RIGHT_READ**

*buffer* type: **zx_info_vmos_t[n]**

The *zx_info_vmos_t* array is list of all VMOs pointed to by the target process.
Some VMOs are mapped, some are pointed to by handles, and some are both.

**Note**: The same VMO may appear multiple times due to multiple
mappings/handles. Also, because VMOs can change as the target process runs,
the same VMO may have different values each time it appears. It is the
caller's job to resolve any duplicates.

To get a full picture of how a process uses its VMOs and how a VMO is used
by various processes, you may need to combine this information with
ZX_INFO_PROCESS_MAPS.

```
// Describes a VMO.
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
} zx_info_vmo_t;
```

See the `vmos` command-line tool for an example user of this topic, and to dump
the VMOs of arbitrary processes by koid.

### ZX_INFO_KMEM_STATS

*handle* type: **Resource** (Specifically, the root resource)

*buffer* type: **zx_info_kmem_stats_t[1]**

Returns information about kernel memory usage. It can be expensive to gather.

```
typedef struct zx_info_kmem_stats {
    // The total amount of physical memory available to the system.
    size_t total_bytes;

    // The amount of unallocated memory.
    size_t free_bytes;

    // The amount of memory reserved by and mapped into the kernel for reasons
    // not covered by other fields in this struct. Typically for readonly data
    // like the ram disk and kernel image, and for early-boot dynamic memory.
    size_t wired_bytes;

    // The amount of memory allocated to the kernel heap.
    size_t total_heap_bytes;

    // The portion of |total_heap_bytes| that is not in use.
    size_t free_heap_bytes;

    // The amount of memory committed to VMOs, both kernel and user.
    // A superset of all userspace memory.
    // Does not include certain VMOs that fall under |wired_bytes|.
    //
    // TODO(dbort): Break this into at least two pieces: userspace VMOs that
    // have koids, and kernel VMOs that don't. Or maybe look at VMOs
    // mapped into the kernel aspace vs. everything else.
    size_t vmo_bytes;

    // The amount of memory used for architecture-specific MMU metadata
    // like page tables.
    size_t mmu_overhead_bytes;

    // Non-free memory that isn't accounted for in any other field.
    size_t other_bytes;
} zx_info_kmem_stats_t;
```

### ZX_INFO_RESOURCE

*handle* type: **Resource**
*buffer* type: **zx_info_resource_t[1]**

Returns information about a resource object via its handle.

```
typedef struct zx_info_resource {
    // The resource kind
    uint32_t kind;
    // Resource's low value (inclusive)
    uint64_t low;
    // Resource's high value (inclusive)
    uint64_t high;
} zx_info_resource_t;
```

The resource kind is one of

*   *ZX_RSRC_KIND_ROOT*
*   *ZX_RSRC_KIND_MMIO*
*   *ZX_RSRC_KIND_IOPORT*
*   *ZX_RSRC_KIND_IRQ*
*   *ZX_RSRC_KIND_HYPERVISOR*
### ZX_INFO_BTI

*handle* type: **Bus Transaction Initiator**

*buffer* type: **zx_info_bti_t[1]**

```
typedef struct zx_info_bti {
    // zx_bti_pin will always be able to return addreses that are contiguous for at
    // least this many bytes.  E.g. if this returns 1MB, then a call to
    // zx_bti_pin() with a size of 2MB will return at most two physically-contiguous runs.
    // If the size were 2.5MB, it will return at most three physically-contiguous runs.
    uint64_t minimum_contiguity;

    // The number of bytes in the device's address space (UINT64_MAX if 2^64).
    uint64_t aspace_size;
} zx_info_bti_t;
```

## RETURN VALUE

**zx_object_get_info**() returns **ZX_OK** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not an appropriate type for *topic*

**ZX_ERR_ACCESS_DENIED**: If *handle* does not have the necessary rights for the
operation.

**ZX_ERR_INVALID_ARGS** *buffer*, *actual*, or *avail* are invalid pointers.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_BUFFER_TOO_SMALL** The *topic* returns a fixed number of records, but the
provided buffer is not large enough for these records.

**ZX_ERR_NOT_SUPPORTED** *topic* does not exist.

## EXAMPLES

```
bool is_handle_valid(zx_handle_t handle) {
    return zx_object_get_info(
        handle, ZX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL) == ZX_OK;
}

zx_koid_t get_object_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC,
                           &info, sizeof(info), NULL, NULL) != ZX_OK) {
        return 0;
    }
    return info.koid;
}

void examine_threads(zx_handle_t proc) {
    zx_koid_t threads[128];
    size_t count, avail;

    if (zx_object_get_info(proc, ZX_INFO_PROCESS_THREADS, threads,
                           sizeof(threads), &count, &avail) != ZX_OK) {
        // Error!
    } else {
        if (avail > count) {
            // More threads than space in array;
            // could call again with larger array.
        }
        for (size_t n = 0; n < count; n++) {
            do_something(thread[n]);
        }
    }
}
```

## SEE ALSO

[handle_close](handle_close.md), [handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md), [object_get_child](object_get_child.md).
