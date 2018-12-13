# zx_vmar_destroy

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

vmar_destroy - destroy a virtual memory address region

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_vmar_destroy(zx_handle_t handle);
```

## DESCRIPTION

`zx_vmar_destroy()` unmaps all mappings within the given region, and destroys
all sub-regions of the region.  Note that this operation is logically recursive.

This operation does not close *handle*.  Any outstanding handles to this
VMAR will remain valid handles, but all VMAR operations on them will fail.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_vmar_destroy()` returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMAR handle.

**ZX_ERR_BAD_STATE**  This region is already destroyed.

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_map](vmar_map.md),
[vmar_protect](vmar_protect.md),
[vmar_unmap](vmar_unmap.md).
