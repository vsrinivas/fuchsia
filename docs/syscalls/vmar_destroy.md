# mx_vmar_destroy

## NAME

vmar_destroy - destroy a virtual memory address region

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmar_destroy(mx_handle_t vmar_handle);
```

## DESCRIPTION

**vmar_destroy**() unmaps all mappings within the given region, and destroys
all sub-regions of the region.  Note that this operation is logically recursive.

This operation does not close *vmar_handle*.  Any outstanding handles to this
VMAR will remain valid handles, but all VMAR operations on them will fail.

## RETURN VALUE

**vmar_destroy**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *vmar_handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *vmar_handle* is not a VMAR handle.

**MX_ERR_BAD_STATE**  This region is already destroyed.

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_map](vmar_map.md),
[vmar_protect](vmar_protect.md),
[vmar_unmap](vmar_unmap.md).
