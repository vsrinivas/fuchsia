# mx_vmar_unmap

## NAME

vmar_unmap - unmap virtual memory pages

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmar_unmap(mx_handle_t vmar_handle,
                          uintptr_t addr, size_t len);
```

## DESCRIPTION

**vmar_unmap**() unmaps all VMO mappings and destroys (as if **vmar_destroy**
were called) all sub-regions within the absolute range including *addr* and ending
before exclusively at *addr* + *len*.  Any sub-region that is in the range must
be fully in the range (i.e. partial overlaps are an error).  If a mapping is
only partially in the range, the mapping is split and the requested portion is
unmapped.

If *len* is not page-aligned, it will be rounded up the next page boundary.

## RETURN VALUE

**vmar_unmap**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *vmar_handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *vmar_handle* is not a VMAR handle.

**MX_ERR_INVALID_ARGS**  *addr* is not page-aligned, *len* is 0, or the
requested range partially overlaps a sub-region.

**MX_ERR_BAD_STATE**  *vmar_handle* refers to a destroyed handle.

**MX_ERR_NOT_FOUND**  Could not find the requested mapping.

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_destroy](vmar_destroy.md),
[vmar_map](vmar_map.md),
[vmar_protect](vmar_protect.md).
