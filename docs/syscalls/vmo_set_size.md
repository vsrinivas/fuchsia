# mx_vmo_set_size

## NAME

vmo_set_size - resize a VMO object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_set_size(mx_handle_t handle, uint64_t size);

```

## DESCRIPTION

**vmo_set_size**() sets the new size of a VMO object.

## RETURN VALUE

**vmo_set_size**() returns **MX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**MX_ERR_ACCESS_DENIED**  *handle* does not have the **MX_RIGHT_WRITE** right.

**MX_ERR_OUT_OF_RANGE**  Requested size is too large.

**MX_ERR_NO_MEMORY**  Failure due to lack of system memory.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md).
