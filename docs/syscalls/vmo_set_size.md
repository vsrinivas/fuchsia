# zx_vmo_set_size

## NAME

vmo_set_size - resize a VMO object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_set_size(zx_handle_t handle, uint64_t size);

```

## DESCRIPTION

**vmo_set_size**() sets the new size of a VMO object.

The size will be rounded up to the next page size boundary.
Subsequent calls to **vmo_get_size**() will return the rounded up size.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vmo_set_size**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have the **ZX_RIGHT_WRITE** right.

**ZX_ERR_UNAVAILABLE** The VMO was created with **ZX_VMO_NON_RESIZABLE** option.

**ZX_ERR_OUT_OF_RANGE**  Requested size is too large.

**ZX_ERR_NO_MEMORY**  Failure due to lack of system memory.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md).
