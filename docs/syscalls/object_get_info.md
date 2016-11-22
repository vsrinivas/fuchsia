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

**object_get_info()** requests information about the provided handle (or the object
the handle refers to).  The *topic* parameter indicates what specific information is desired.

*buffer* is a pointer to a buffer of size *buffer_size* to return the information.

*actual* is an optional pointer to return the number of records that were written to buffer.

*avail* is an optional pointer to return the number of records that are available to read.

If the buffer is insufficiently large, *avail* will be larger than *actual*.


## TOPICS

**MX_INFO_HANDLE_VALID**  No records are returned and *buffer* may be NULL.  This query
succeeds as long as *handle* is a valid handle.

**MX_INFO_HANDLE_BASIC**  Always returns a single *mx_info_handle_basic_t* record containing
information about the handle:  The kernel object id of the object it refers to, the rights
associated with it, the type of object it refers to, and some property information.

**MX_INFO_PROCESS**  Requires a Process handle.  Always returns a single *mx_info_process_t*
record containing the return code of a process, if the process has exited.

**MX_INFO_PROCESS_THREADS**  Requires a Process handle. Returns an array of *mx_koid_t*, one for
each thread in the Process at that moment in time.

**MX_INFO_RESOURCE_CHILDREN**  Requires a Resource handle.  Returns an array of *mx_rrec_t*,
one for each child Resource of the provided Resource handle.

**MX_INFO_RESOURCE_RECORDS**  Requires a Resource handle.  Returns an array of *mx_rrec_t*,
one for each Record associated with the provided Resource handle.

**MX_INFO_VMAR**  Requires a VM Address Region handle.  Always returns a single of *mx_info_vmar_t*,
record containing the base and length of the region.


## RETURN VALUE

**mx_object_get_info**() returns **NO_ERROR** on success. In the event of failure, a negative error
value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not an appropriate type for *topic*

**ERR_INVALID_ARGS**  *buffer*, *actual*, or *avail* are invalid pointers.

**ERR_NO_MEMORY**  Temporary out of memory failure.

**ERR_BUFFER_TOO_SMALL**  The *topic* returns a fixed number of records, but the provided buffer
is not large enough for these records.

**ERR_NOT_SUPPORTED**  *topic* does not exist.


## EXAMPLES

```
bool is_handle_valid(mx_handle_t handle) {
    return mx_object_get_info(handle, MX_INFO_HANDLE_VALID, NULL, 0, NULL, NULL) == NO_ERROR;
}


mx_koid_t get_object_koid(mx_handle_t handle) {
    mx_info_handle_basic_t info;
    if (mx_object_get_info(handle, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL)) {
        return 0;
    } else {
        return info.koid;
    }
}


mx_koid_t threads[128];
size_t count, avail;

if (mx_object_get_info(proc, MX_INFO_PROCESS_THREADS, threads,
                       sizeof(threads), &count, &avail) < 0) {
    // error!
} else {
    if (avail > count) {
        // more threads than space in array, could call again with larger array
    }
    for (unsigned n = 0; n < count; n++) {
        do_something(thread[n]);
    }
}
```
