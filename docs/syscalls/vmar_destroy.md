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

**vmar_destroy**() returns **NO_ERROR** on success.

## ERRORS

**ERR_BAD_STATE**  This region is already destroyed

## NOTES

## SEE ALSO

[vmar_allocate](vmar_allocate.md).
[vmar_unmap](vmar_unmap.md).
