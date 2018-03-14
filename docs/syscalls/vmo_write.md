# zx_vmo_write

## NAME

vmo_write - write bytes to the VMO

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_write(zx_handle_t handle, const void* data, uint64_t offset,
                         size_t len);

```

## DESCRIPTION

**vmo_write**() attempts to write exactly *len* bytes to a VMO at *offset*.

*data* pointer to a user buffer to write bytes from.

*len* number of bytes to attempt to write.

## RETURN VALUE

**zx_vmo_write**() returns **ZX_OK** on success, and exactly *len* bytes will
have been written from *data*.
In the event of failure, a negative error value is returned, and the number of
bytes written from *data* is undefined.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have the **ZX_RIGHT_WRITE** right.

**ZX_ERR_INVALID_ARGS**  *data* is an invalid pointer or NULL.

**ZX_ERR_NO_MEMORY**  Failure to allocate system memory to complete write.

**ZX_ERR_OUT_OF_RANGE**  *offset* starts at or beyond the end of the VMO,
                         or VMO is shorter than *len*.

**ZX_ERR_BAD_STATE**  VMO has been marked uncached and is not directly writable.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_read](vmo_read.md),
[vmo_get_size](vmo_get_size.md),
[vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
[vmo_set_cache_policy](vmo_set_cache_policy.md)
