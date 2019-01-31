# zx_vmo_get_size

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

vmo_get_size - read the current size of a VMO object

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_get_size(zx_handle_t handle, uint64_t* size);
```

## DESCRIPTION

`zx_vmo_get_size()` returns the current size of the VMO.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_vmo_get_size()` returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**ZX_ERR_INVALID_ARGS**  *size* is an invalid pointer or NULL.

## SEE ALSO

 - [`zx_vmo_clone()`]
 - [`zx_vmo_create()`]
 - [`zx_vmo_op_range()`]
 - [`zx_vmo_read()`]
 - [`zx_vmo_set_size()`]
 - [`zx_vmo_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_vmo_clone()`]: vmo_clone.md
[`zx_vmo_create()`]: vmo_create.md
[`zx_vmo_op_range()`]: vmo_op_range.md
[`zx_vmo_read()`]: vmo_read.md
[`zx_vmo_set_size()`]: vmo_set_size.md
[`zx_vmo_write()`]: vmo_write.md
