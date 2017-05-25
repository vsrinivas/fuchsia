# mx_object_get_info

## NAME

object_get_info - query information about an object

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

mx_status_t mx_object_get_info(mx_handle_t handle, uint32_t topic,
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

## TOPICS

### MX_INFO_HANDLE_VALID

*handle* type: **Any**

*buffer* type: **n/a**

Returns **NO_ERROR** if *handle* is valid, a negative status otherwise. No
records are returned and *buffer* may be NULL.

### MX_INFO_HANDLE_BASIC

*handle* type: **Any**

*buffer* type: **mx_info_handle_basic_t[1]**

```
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
```

### MX_INFO_PROCESS

*handle* type: **Process**

*buffer* type: **mx_info_process_t[1]**

```
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
```

### MX_INFO_PROCESS_THREADS

*handle* type: **Process**

*buffer* type: **mx_koid_t[n]**

Returns an array of *mx_koid_t*, one for each thread in the Process at that
moment in time.

### MX_INFO_RESOURCE_CHILDREN

*handle* type: **Resource**

*buffer* type: **mx_rrec_t[n]**

Returns an array of *mx_rrec_t*, one for each child Resource of the provided
Resource handle.

### MX_INFO_RESOURCE_RECORDS

*handle* type: **Resource**

*buffer* type: **mx_rrec_t[n]**

Returns an array of *mx_rrec_t*, one for each Record associated with the
provided Resource handle.

### MX_INFO_THREAD

*handle* type: **Thread**

*buffer* type: **mx_info_thread_t[1]**

```
typedef struct mx_info_thread {
    // If nonzero, the thread has gotten an exception and is waiting for
    // the exception to be handled by the specified port.
    // The value is one of MX_EXCEPTION_PORT_TYPE_*.
    uint32_t wait_exception_port_type;
} mx_info_thread_t;
```

The **MX_EXCEPTION_PORT_TYPE_\*** values are defined by

```
#include <magenta/syscalls/exception.h>
```

*   *MX_EXCEPTION_PORT_TYPE_NONE*
*   *MX_EXCEPTION_PORT_TYPE_DEBUGGER*
*   *MX_EXCEPTION_PORT_TYPE_THREAD*
*   *MX_EXCEPTION_PORT_TYPE_PROCESS*
*   *MX_EXCEPTION_PORT_TYPE_SYSTEM*

### MX_INFO_THREAD_EXCEPTION_REPORT

*handle* type: **Thread**

*buffer* type: **mx_exception_report_t[1]**

```
#include <magenta/syscalls/exception.h>
```

If the thread is currently in an exception and is waiting for an exception
response, then this returns the exception report as a single
*mx_exception_report_t*, with status NO_ERROR.

Returns **ERR_BAD_STATE** if the thread is not in an exception and waiting for
an exception response.

### MX_INFO_VMAR

*handle* type: **VM Address Region**

*buffer* type: **mx_info_vmar_t[1]**

```
typedef struct mx_info_vmar {
    // Base address of the region.
    uintptr_t base;

    // Length of the region, in bytes.
    size_t len;
} mx_info_vmar_t;
```

### MX_INFO_JOB_CHILDREN

*handle* type: **Job**

*buffer* type: **mx_koid_t[n]**

Returns an array of *mx_koid_t*, one for each direct child Job of the provided
Job handle.

### MX_INFO_JOB_PROCESSES

*handle* type: **Job**

*buffer* type: **mx_koid_t[n]**

Returns an array of *mx_koid_t*, one for each direct child Process of the
provided Job handle.

### MX_INFO_TASK_STATS

*handle* type: **Process**

*buffer* type: **mx_info_task_stats_t[1]**

```
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
```

Additional errors:

*   **ERR_BAD_STATE**: If the target process is not currently running.

### MX_INFO_PROCESS_MAPS

*handle* type: **Process** other than your own, with **MX_RIGHT_READ**

*buffer* type: **mx_info_maps_t[n]**

The *mx_info_maps_t* array is a depth-first pre-order walk of the target
process's Aspace/VMAR/Mapping tree.

```
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
```

The *depth* field of each entry describes its relationship to the nodes that
come before it. Depth 0 is the root Aspace, depth 1 is the root VMAR, and all
other entries have depth 2 or greater.

See the `vmaps` command-line tool for an example user of this topic, and to dump
the maps of arbitrary processes by koid.

Additional errors:

*   **ERR_ACCESS_DENIED**: If the appropriate rights are missing, or if a
    process attempts to call this on a handle to itself. It's not safe to
    examine yourself: *buffer* will live inside the Aspace being examined, and
    the kernel can't safely fault in pages of the buffer while walking the
    Aspace.
*   **ERR_BAD_STATE**: If the target process is not currently running, or if
    its address space has been destroyed.

## RETURN VALUE

**mx_object_get_info**() returns **NO_ERROR** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ERR_BAD_HANDLE** *handle* is not a valid handle.

**ERR_WRONG_TYPE** *handle* is not an appropriate type for *topic*

**ERR_ACCESS_DENIED**: If *handle* does not have the necessary rights for the
operation.

**ERR_INVALID_ARGS** *buffer*, *actual*, or *avail* are invalid pointers.

**ERR_NO_MEMORY** Temporary out of memory failure.

**ERR_BUFFER_TOO_SMALL** The *topic* returns a fixed number of records, but the
provided buffer is not large enough for these records.

**ERR_NOT_SUPPORTED** *topic* does not exist.

## EXAMPLES

```
bool is_handle_valid(mx_handle_t handle) {
    return mx_object_get_info(
        handle, MX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL) == NO_ERROR;
}

mx_koid_t get_object_koid(mx_handle_t handle) {
    mx_info_handle_basic_t info;
    if (mx_object_get_info(handle, MX_INFO_HANDLE_BASIC,
                           &info, sizeof(info), NULL, NULL) != NO_ERROR) {
        return 0;
    }
    return info.koid;
}

void examine_threads(mx_handle_t proc) {
    mx_koid_t threads[128];
    size_t count, avail;

    if (mx_object_get_info(proc, MX_INFO_PROCESS_THREADS, threads,
                           sizeof(threads), &count, &avail) != NO_ERROR) {
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
