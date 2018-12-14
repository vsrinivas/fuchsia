# zx_vmar_map

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

vmar_map - add a memory mapping

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_vmar_map(zx_handle_t handle,
                        zx_vm_option_t options,
                        uint64_t vmar_offset,
                        zx_handle_t vmo,
                        uint64_t vmo_offset,
                        uint64_t len,
                        zx_vaddr_t* mapped_addr);
```

## DESCRIPTION

Maps the given VMO into the given virtual memory address region.  The mapping
retains a reference to the underlying virtual memory object, which means
closing the VMO handle does not remove the mapping added by this function.

*options* is a bit vector of the following:
- **ZX_VM_SPECIFIC**  Use the *vmar_offset* to place the mapping, invalid if
  *handle* does not have the **ZX_VM_CAN_MAP_SPECIFIC** permission.
  *vmar_offset* is an offset relative to the base address of the given VMAR.
  It is an error to specify a range that overlaps with another VMAR or mapping.
- **ZX_VM_SPECIFIC_OVERWRITE**  Same as **ZX_VM_SPECIFIC**, but can
  overlap another mapping.  It is still an error to partially-overlap another VMAR.
  If the range meets these requirements, it will atomically (with respect to all
  other map/unmap/protect operations) replace existing mappings in the area.
- **ZX_VM_PERM_READ**  Map *vmo* as readable.  It is an error if *handle*
  does not have **ZX_VM_CAN_MAP_READ** permissions, the *handle* does
  not have the **ZX_RIGHT_READ** right, or the *vmo* handle does not have the
  **ZX_RIGHT_READ** right.
- **ZX_VM_PERM_WRITE**  Map *vmo* as writable.  It is an error if *handle*
  does not have **ZX_VM_CAN_MAP_WRITE** permissions, the *handle* does
  not have the **ZX_RIGHT_WRITE** right, or the *vmo* handle does not have the
  **ZX_RIGHT_WRITE** right.
- **ZX_VM_PERM_EXECUTE**  Map *vmo* as executable.  It is an error if *handle*
  does not have **ZX_VM_CAN_MAP_EXECUTE** permissions, the *handle* handle does
  not have the **ZX_RIGHT_EXECUTE** right, or the *vmo* handle does not have the
  **ZX_RIGHT_EXECUTE** right.
- **ZX_VM_MAP_RANGE**  Immediately page into the new mapping all backed
  regions of the VMO.  This cannot be specified if
  **ZX_VM_SPECIFIC_OVERWRITE** is used.
- **ZX_VM_REQUIRE_NON_RESIZABLE** Maps the VMO only if the VMO is non-resizable,
  that is, it was created with the **ZX_VMO_NON_RESIZABLE** option.

*vmar_offset* must be 0 if *options* does not have **ZX_VM_SPECIFIC** or
**ZX_VM_SPECIFIC_OVERWRITE** set.  If neither of those are set, then
the mapping will be assigned an offset at random by the kernel (with an
allocator determined by policy set on the target VMAR).

*len* must be page-aligned.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_VMAR**.

*vmo* must be of type **ZX_OBJ_TYPE_VMO**.

## RETURN VALUE

`zx_vmar_map()` returns **ZX_OK** and the absolute base address of the
mapping (via *mapped_addr*) on success.  The base address will be page-aligned
and non-zero.  In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* or *vmo* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* or *vmo* is not a VMAR or VMO handle, respectively.

**ZX_ERR_BAD_STATE**  *handle* refers to a destroyed VMAR.

**ZX_ERR_INVALID_ARGS** *mapped_addr* or *options* are not valid, *vmar_offset* is
non-zero when neither **ZX_VM_SPECIFIC** nor
**ZX_VM_SPECIFIC_OVERWRITE** are given,
**ZX_VM_SPECIFIC_OVERWRITE** and **ZX_VM_MAP_RANGE** are both given,
*vmar_offset* and *len* describe an unsatisfiable allocation due to exceeding the region bounds,
*vmar_offset* or *vmo_offset* or *len* are not page-aligned,
`vmo_offset + ROUNDUP(len, PAGE_SIZE)` overflows.

**ZX_ERR_ACCESS_DENIED**  Insufficient privileges to make the requested mapping.

**ZX_ERR_NOT_SUPPORTED** The VMO is resizable and **ZX_VM_REQUIRE_NON_RESIZABLE** was
requested.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## NOTES

The VMO that backs a memory mapping can be resized to a smaller size. This can cause the
thread is reading or writing to the VMAR region to fault. To avoid this hazard, services
that receive VMOs from clients should use **ZX_VM_REQUIRE_NON_RESIZABLE** when mapping
the VMO.

## SEE ALSO

 - [`zx_vmar_allocate()`]
 - [`zx_vmar_destroy()`]
 - [`zx_vmar_protect()`]
 - [`zx_vmar_unmap()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_vmar_allocate()`]: vmar_allocate.md
[`zx_vmar_destroy()`]: vmar_destroy.md
[`zx_vmar_protect()`]: vmar_protect.md
[`zx_vmar_unmap()`]: vmar_unmap.md
