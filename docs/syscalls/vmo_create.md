# zx_vmo_create

## NAME

vmo_create - create a VM object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_create(uint64_t size, uint32_t options, zx_handle_t* out);

```

## DESCRIPTION

**vmo_create**() creates a new virtual memory object (VMO), which represents
a container of zero to *size* bytes of memory managed by the operating
system.

The size of the VMO will be rounded up to the next page size boundary.
Use **vmo_get_size**() to return the current size of the VMO.

One handle is returned on success, representing an object with the requested
size.

The following rights will be set on the handle by default:

**ZX_RIGHT_DUPLICATE** - The handle may be duplicated.

**ZX_RIGHT_TRANSFER** - The handle may be transferred to another process.

**ZX_RIGHT_READ** - May be read from or mapped with read permissions.

**ZX_RIGHT_WRITE** - May be written to or mapped with write permissions.

**ZX_RIGHT_EXECUTE** - May be mapped with execute permissions.

**ZX_RIGHT_MAP** - May be mapped.

**ZX_RIGHT_GET_PROPERTY** - May get its properties using
[object_get_property](object_get_property).

**ZX_RIGHT_SET_PROPERTY** - May set its properties using
[object_set_property](object_set_property).

The *options* field can be 0 or **ZX_VMO_NON_RESIZABLE** to create a VMO
that cannot change size. Clones of a non-resizable VMO can be resized.

The **ZX_VMO_ZERO_CHILDREN** signal is active on a newly created VMO. It becomes
inactive whenever a clone of the VMO is created and becomes active again when
all clones have been destroyed and no mappings of those clones into address
spaces exist.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vmo_create**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL or *options* is
any value other than 0.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[vmo_read](vmo_read.md),
[vmo_write](vmo_write.md),
[vmo_clone](vmo_clone.md),
[vmo_set_size](vmo_set_size.md),
[vmo_get_size](vmo_get_size.md),
[vmo_op_range](vmo_op_range.md),
[vmar_map](vmar_map.md).
