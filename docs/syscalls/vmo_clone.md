# mx_vmo_clone

## NAME

vmo_clone - create a clone of a VM Object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_clone(mx_handle_t handle, uint32_t options, uint64_t offset, uint64_t size, mx_handle_t* out);

```

## DESCRIPTION

**vmo_clone**() creates a new virtual memory object (VMO) that clones a range
of an existing vmo.

One handle is returned on success, representing an object with the requested
size.

*options* must contain one or more flags to control clone creation.

Valid flags:

- *MX_VMO_CLONE_COPY_ON_WRITE* - Create a copy-on-write clone. The cloned vmo will
behave the same way the parent does, except that any write operation on the clone
will bring in a copy of the page at the offset the write occurred. The new page in
the cloned vmo is now a copy and may diverge from the parent. Any reads from
ranges outside of the parent vmo's size will contain zeros, and writes will
allocate new zero filled pages.

*offset* must be page aligned.

*offset* + *size* may not exceed the range of a 64bit unsigned value.

Both offset and size may start or extend beyond the original VMO's size.

By default the rights of the cloned handled will be the same as the
original with a few exceptions. See [vmo_create](vmo_create.md) for a
discussion of the details of each right.

If *options* is *MX_VMO_CLONE_COPY_ON_WRITE* the following rights are added:

- **MX_RIGHT_WRITE**

*TEMPORARY* The following rights are added:

- **MX_RIGHT_EXECUTE**

- **MX_RIGHT_MAP**

## RETURN VALUE

**vmo_clone**() returns **NO_ERROR** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_TYPE**  Input handle is not a VMO.

**ERR_ACCESS_DENIED**  Input handle does not have sufficient rights.

**ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL
or the offset is not page aligned.

**ERR_OUT_OF_RANGE**  *offset* + *size* is too large.

**ERR_NO_MEMORY**  Failure due to lack of memory.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_set_size](vmo_set_size.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md),
[vmar_map](vmar_map.md).
