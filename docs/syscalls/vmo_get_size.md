# zx_vmo_get_size

## NAME

vmo_get_size - read the current size of a VMO object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_get_size(zx_handle_t handle, uint64_t* size);

```

## DESCRIPTION

**vmo_get_size**() returns the current size of the VMO.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vmo_get_size**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**ZX_ERR_INVALID_ARGS**  *size* is an invalid pointer or NULL.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
