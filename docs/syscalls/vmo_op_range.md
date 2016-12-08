# mx_vmo_op_range

## NAME

vmo_op_range - perform an operation on a range of a VMO

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t vmo_op_range(mx_handle_t handle, uint32_t op,
                         uint64_t offset, uint64_t size,
                         void* buffer, size_t buffer_size);

```

## DESCRIPTION

TODO: fill in

## RETURN VALUE

**vmo_op_range**() returns **NO_ERROR** on success. In the event of failure, a negative error
value is returned.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a VMO handle.

TODO: fill in

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md),
[vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
