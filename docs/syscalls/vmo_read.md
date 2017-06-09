# mx_vmo_read

## NAME

vmo_read - read bytes from the VMO

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_read(mx_handle_t handle, void* data, uint64_t offset, size_t len,
                        size_t* actual);

```

## DESCRIPTION

**vmo_read**() attempts to read *len* bytes from a VMO at *offset*. The number of actual
bytes read is returned in *actual*.

*data* pointer to a user buffer to read bytes into.

*len* number of bytes to attempt to read. *data* buffer should be large enough for at least this
many bytes.

*actual* returns the actual number of bytes read, which may be anywhere from 0 to *len*. If
a read extends beyond the size of the VMO, the actual bytes read will be trimmed. If the
read starts at or beyond the size of the VMO, **MX_ERR_OUT_OF_RANGE** will be returned.

## RETURN VALUE

**mx_vmo_read**() returns **MX_OK** on success. In the event of failure, a negative error
value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**MX_ERR_ACCESS_DENIED**  *handle* does not have the **MX_RIGHT_READ** right.

**MX_ERR_INVALID_ARGS**  *actual* or *data* is an invalid pointer or NULL.

**MX_ERR_OUT_OF_RANGE**  *offset* starts at or beyond the end of the VMO.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md),
[vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
