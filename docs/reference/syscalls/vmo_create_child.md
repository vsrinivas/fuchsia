# zx_vmo_create_child

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Create a child of a VM Object.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_vmo_create_child(zx_handle_t handle,
                                uint32_t options,
                                uint64_t offset,
                                uint64_t size,
                                zx_handle_t* out);
```

## DESCRIPTION

`zx_vmo_create_child()` creates a new virtual memory object (VMO) a child of
an existing vmo. The behavior of the semantics depends on the type of the child.

One handle is returned on success, representing an object with the requested
size.

*options* must contain exactly one of the following flags to specify the
child type:

- **ZX_VMO_CHILD_COPY_ON_WRITE** - Create a copy-on-write clone. The cloned vmo will
behave the same way the parent does, except that any write operation on the clone
will bring in a copy of the page at the offset the write occurred. The new page in
the cloned vmo is now a copy and may diverge from the parent. Any reads from
ranges outside of the parent vmo's size will contain zeros, and writes will
allocate new zero filled pages. A vmo which has pinned regions cannot be cloned. See
the NOTES section below for details on VMO syscall interactions with clones. This flag
may not be used for vmos created with [`zx_vmo_create_physical()`] or descendants of
such a vmo.

- **ZX_VMO_CHILD_SLICE** - Create a slice that has direct read/write access into
a section of the parent. All operations on the slice vmo behave as if they were
done on the parent. A slice differs from a duplicate handle to the parent by allowing
access to only a subrange of the parent vmo, and allowing for the
**ZX_VMO_ZERO_CHILDREN** signal to be used. This flag may be used with vmos created with
[`zx_vmo_create_physical()`] and their descendants.

- **ZX_VMO_CHILD_PRIVATE_PAGER_COPY** - Create a private copy of a pager vmo. The child
vmo will behave the same way the parent does, except that any write operation on the
child will bring in a copy of the page at the offset the write occurred into the child
vmo. The new page in the child vmo is now a copy and may diverge from the parent. Any
reads from ranges outside of the parent vmo's size will contain zeros, and writes will
allocate new zero filled pages.  See the NOTES section below for details on VMO syscall
interactions with child. This flag is only supported for vmos created with
[`zx_pager_create_vmo()`] or descendants of such a vmo.

In addition, *options* can contain zero or more of the following flags to
further specify the child's behavior:

- **ZX_VMO_CHILD_RESIZEABLE** - Create a resizeable child VMO.

- **ZX_VMO_CHILD_NO_WRITE** - Create a child that cannot be written to.

*offset* must be page aligned.

*offset* + *size* may not exceed the range of a 64bit unsigned value.

Both offset and size may start or extend beyond the original VMO's size.

The size of the VMO will be rounded up to the next page size boundary.

By default the rights of the child handled will be the same as the
original with a few exceptions. See [`zx_vmo_create()`] for a
discussion of the details of each right.

In all cases if **ZX_VMO_NO_WRITE** is set then **ZX_RIGHT_WRITE** will be removed.

If *options* is **ZX_VMO_CHILD_COPY_ON_WRITE** or **ZX_VMO_CHILD_PRIVATE_PAGER_COPY** and
**ZX_VMO_CHILD_NO_WRITE** is not set then **ZX_RIGHT_WRITE** will be added and **ZX_RIGHT_EXECUTE**
will be removed.

## NOTES

Creating a child VMO causes the existing (source) VMO **ZX_VMO_ZERO_CHILDREN** signal
to become inactive. Only when the last child is destroyed and no mappings
of those child into address spaces exist, will **ZX_VMO_ZERO_CHILDREN** become
active again.

Non-slice child vmos will interact with the VMO syscalls in the following ways:

- The COMMIT mode of [`zx_vmo_op_range()`] on a child will commit pages into the child that
  have the same content as its parent's corresponding pages. If those pages are supplied by a
  pager, this operation will also commit those pages in the parent. Otherwise, if those pages
  are not comitted in the parent, zero-filled pages will be comitted directly into
  child, without affecting the parent.
- The DECOMMIT mode of [`zx_vmo_op_range()`] is not supported.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_VMO** and have **ZX_RIGHT_DUPLICATE** and have **ZX_RIGHT_READ**.

## RETURN VALUE

`zx_vmo_create_child()` returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_BAD_TYPE**  Input handle is not a VMO.

**ZX_ERR_ACCESS_DENIED**  Input handle does not have sufficient rights.

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL
or the offset is not page aligned.

**ZX_ERR_OUT_OF_RANGE**  *offset* + *size* is too large.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ERR_BAD_STATE**  A COW child could not be created because the vmo has some
pinned pages.

## SEE ALSO

 - [`zx_vmar_map()`]
 - [`zx_vmo_create()`]
 - [`zx_vmo_get_size()`]
 - [`zx_vmo_op_range()`]
 - [`zx_vmo_read()`]
 - [`zx_vmo_set_size()`]
 - [`zx_vmo_write()`]

<!-- References updated by update-docs-from-fidl, do not edit. -->

[`zx_pager_create_vmo()`]: pager_create_vmo.md
[`zx_vmar_map()`]: vmar_map.md
[`zx_vmo_create()`]: vmo_create.md
[`zx_vmo_create_physical()`]: vmo_create_physical.md
[`zx_vmo_get_size()`]: vmo_get_size.md
[`zx_vmo_op_range()`]: vmo_op_range.md
[`zx_vmo_read()`]: vmo_read.md
[`zx_vmo_set_size()`]: vmo_set_size.md
[`zx_vmo_write()`]: vmo_write.md
