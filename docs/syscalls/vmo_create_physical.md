# zx_vmo_create_physical

## NAME

vmo_create_physical- create a VM object referring to a specific contiguous range
of physical memory

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmo_create_physical(zx_handle_t resource, zx_paddr_t paddr,
                                   size_t size, zx_handle_t* out);
```

## DESCRIPTION

**vmo_create_physical**() creates a new virtual memory object (VMO), which represents the
*size* bytes of physical memory beginning at physical address *paddr*.

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

The **ZX_VMO_ZERO_CHILDREN** signal is active on a newly created VMO. It becomes
inactive whenever a clone of the VMO is created and becomes active again when
all clones have been destroyed and no mappings of those clones into address
spaces exist.

## NOTES

The VMOs created by this syscall are not usable with **vmo_read**() and
**vmo_write**().

## RETURN VALUE

**vmo_create_physical**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZER_ERR_WRONG_TYPE** *resource* is not a handle to a Resource object.

**ZER_ERR_ACCESS_DENIED** *resource* does not grant access to the requested
range of memory.

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL, or *paddr* or
*size* are not page-aligned.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[vmar_map](vmar_map.md).
