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

## RETURN VALUE

**vmar_unmap**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *addr* or *len* are not page-aligned, *len* is 0, or the
requested range partially overlaps a sub-region.

**ERR_BAD_STATE**  *vmar_handle* refers to a destroyed handle

**ERR_NOT_FOUND**  could not find the requested mapping

## NOTES

## SEE ALSO

[vmar_destroy](vmar_destroy.md).
[vmar_map](vmar_map.md).
[vmar_protect](vmar_protect.md).
