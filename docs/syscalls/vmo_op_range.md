# mx_vmo_op_range

## NAME

vmo_op_range - perform an operation on a range of a VMO

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmo_op_range(mx_handle_t handle, uint32_t op,
                            uint64_t offset, uint64_t size,
                            void* buffer, size_t buffer_size);

```

## DESCRIPTION

**vmo_op_range()** performs cache and memory operations against pages held by the VMO.

*offset* byte offset specifying the starting location for *op* in the VMO's held memory.

*size* length, in bytes, to perform the operation on.

*op* the operation to perform:

*buffer* and *buffer_size* are used to store the addresses returned by *MX_VMO_OP_LOOKUP*.

**MX_VMO_OP_COMMIT** - Commit *size* bytes worth of pages starting at byte *offset* for the VMO.
More information can be found in the [vm object documentation](../objects/vm_object.md).

**MX_VMO_OP_DECOMMIT** - Release a range of pages previously commited to the VMO from *offset* to *offset*+*size*.

**MX_VMO_OP_LOCK** - Presently unsupported.

**MX_VMO_OP_UNLOCK** - Presently unsupported.

**MX_VMO_OP_LOOKUP** - Returns a list of physical addresses (paddr_t) corresponding to the pages held by the VMO
from *offset* to *offset*+*size*. The result is stored in *buffer*, up to *buffer_size* bytes.
The returned physical addresses are aligned to page boundaries. So if the provided offset
is not page aligned, the first physical address returned will match the beginning of the page containing
the offset, not the actual physical address corresponding to the offset.

**MX_VMO_OP_CACHE_SYNC** - Performs a cache sync operation.

**MX_VMO_OP_CACHE_INVALIDATE** - Performs a cache invalidation operation.

**MX_VMO_OP_CACHE_CLEAN** - Performs a cache clean operation.

**MX_VMO_OP_CACHE_CLEAN_INVALIDATE** - Performs cache clean and invalidate operations together.


## RETURN VALUE

**vmo_op_range**() returns **MX_OK** on success. In the event of failure, a negative error
value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_OUT_OF_RANGE**  An invalid memory range specified by *offset* and *size*.

**MX_ERR_NO_MEMORY**  Allocations to commit pages for *MX_VMO_OP_COMMIT* failed.

**MX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**MX_ERR_INVALID_ARGS**  *out* is an invalid pointer, *op* is not a valid operation, *op* is
*MX_VMO_LOOPUP* and *buffer* is an invalid pointer, or *size* is zero and *op* is a cache operation.

**MX_ERR_NOT_SUPPORTED**  *op* was *MX_VMO_OP_LOCK* or *MX_VMO_OP_UNLOCK*.

## SEE ALSO

[vmo_create](vmo_create.md),
[vmo_clone](vmo_clone.md),
[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_get_size](vmo_get_size.md),
[vmo_set_size](vmo_set_size.md),
[vmo_op_range](vmo_op_range.md).
