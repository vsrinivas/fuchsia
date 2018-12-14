# zx_vmo_op_range

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

vmo_op_range - perform an operation on a range of a VMO

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_op_range(zx_handle_t handle,
                            uint32_t op,
                            uint64_t offset,
                            uint64_t size,
                            void* buffer,
                            size_t buffer_size);
```

## DESCRIPTION

`zx_vmo_op_range()` performs cache and memory operations against pages held by the VMO.

*offset* byte offset specifying the starting location for *op* in the VMO's held memory.

*size* length, in bytes, to perform the operation on.

*op* the operation to perform:

*buffer* and *buffer_size* are currently unused.

**ZX_VMO_OP_COMMIT** - Commit *size* bytes worth of pages starting at byte *offset* for the VMO.
More information can be found in the [vm object documentation](../objects/vm_object.md).
Requires the **ZX_RIGHT_WRITE** right.

**ZX_VMO_OP_DECOMMIT** - Release a range of pages previously committed to the VMO from *offset* to *offset*+*size*.
Requires the **ZX_RIGHT_WRITE** right.

**ZX_VMO_OP_LOCK** - Presently unsupported.

**ZX_VMO_OP_UNLOCK** - Presently unsupported.

**ZX_VMO_OP_CACHE_SYNC** - Performs a cache sync operation.
Requires the **ZX_RIGHT_READ** right.

**ZX_VMO_OP_CACHE_INVALIDATE** - Performs a cache invalidation operation.
Requires the **ZX_RIGHT_WRITE** right.

**ZX_VMO_OP_CACHE_CLEAN** - Performs a cache clean operation.
Requires the **ZX_RIGHT_READ** right.

**ZX_VMO_OP_CACHE_CLEAN_INVALIDATE** - Performs cache clean and invalidate operations together.
Requires the **ZX_RIGHT_READ** right.


## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

If *op* is **ZX_VMO_OP_COMMIT**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_WRITE**.

If *op* is **ZX_VMO_OP_DECOMMIT**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_WRITE**.

If *op* is **ZX_VMO_OP_CACHE_SYNC**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_READ**.

If *op* is **ZX_VMO_OP_CACHE_INVALIDATE**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_WRITE**.

If *op* is **ZX_VMO_OP_CACHE_CLEAN**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_READ**.

If *op* is **ZX_VMO_OP_CACHE_CLEAN_INVALIDATE**, *handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_READ**.

## RETURN VALUE

`zx_vmo_op_range()` returns **ZX_OK** on success. In the event of failure, a negative error
value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_OUT_OF_RANGE**  An invalid memory range specified by *offset* and *size*.

**ZX_ERR_NO_MEMORY**  Allocations to commit pages for **ZX_VMO_OP_COMMIT** failed.

**ZX_ERR_WRONG_TYPE**  *handle* is not a VMO handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have sufficient rights to perform the operation.

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer, *op* is not a valid
operation, or *size* is zero and *op* is a cache operation.

**ZX_ERR_NOT_SUPPORTED**  *op* was **ZX_VMO_OP_LOCK** or **ZX_VMO_OP_UNLOCK**, or
*op* was **ZX_VMO_OP_DECOMMIT** and the underlying VMO does not allow decommiting.

## SEE ALSO

 - [`zx_vmo_clone()`]
 - [`zx_vmo_create()`]
 - [`zx_vmo_get_size()`]
 - [`zx_vmo_read()`]
 - [`zx_vmo_set_size()`]
 - [`zx_vmo_write()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_vmo_clone()`]: vmo_clone.md
[`zx_vmo_create()`]: vmo_create.md
[`zx_vmo_get_size()`]: vmo_get_size.md
[`zx_vmo_read()`]: vmo_read.md
[`zx_vmo_set_size()`]: vmo_set_size.md
[`zx_vmo_write()`]: vmo_write.md
