# zx_vmar_map

## NAME

vmar_map - add a memory mapping

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vmar_map(zx_handle_t vmar, uint64_t vmar_offset,
                        zx_handle_t vmo, uint64_t vmo_offset, uint64_t len,
                        uint32_t map_flags, zx_vaddr_t* mapped_addr)
```

## DESCRIPTION

Maps the given VMO into the given virtual memory address region.  The mapping
retains a reference to the underlying virtual memory object, which means
closing the VMO handle does not remove the mapping added by this function.

*map_flags* is a bit vector of the following flags:
- **ZX_VM_FLAG_SPECIFIC**  Use the *vmar_offset* to place the mapping, invalid if
  vmar does not have the **ZX_VM_FLAG_CAN_MAP_SPECIFIC** permission.
  *vmar_offset* is an offset relative to the base address of the given VMAR.
  It is an error to specify a range that overlaps with another VMAR or mapping.
- **ZX_VM_FLAG_SPECIFIC_OVERWRITE**  Same as **ZX_VM_FLAG_SPECIFIC**, but can
  overlap another mapping.  It is still an error to partially-overlap another VMAR.
  If the range meets these requirements, it will atomically (with respect to all
  other map/unmap/protect operations) replace existing mappings in the area.
- **ZX_VM_FLAG_PERM_READ**  Map *vmo* as readable.  It is an error if *vmar*
  does not have *ZX_VM_FLAG_CAN_MAP_READ* permissions, the *vmar* handle does
  not have the *ZX_RIGHT_READ* right, or the *vmo* handle does not have the
  *ZX_RIGHT_READ* right.
- **ZX_VM_FLAG_PERM_WRITE**  Map *vmo* as writable.  It is an error if *vmar*
  does not have *ZX_VM_FLAG_CAN_MAP_WRITE* permissions, the *vmar* handle does
  not have the *ZX_RIGHT_WRITE* right, or the *vmo* handle does not have the
  *ZX_RIGHT_WRITE* right.
- **ZX_VM_FLAG_PERM_EXECUTE**  Map *vmo* as executable.  It is an error if *vmar*
  does not have *ZX_VM_FLAG_CAN_MAP_EXECUTE* permissions, the *vmar* handle does
  not have the *ZX_RIGHT_EXECUTE* right, or the *vmo* handle does not have the
  *ZX_RIGHT_EXECUTE* right.
- **ZX_VM_FLAG_MAP_RANGE**  Immediately page into the new mapping all backed
  regions of the VMO.  This cannot be specified if
  *ZX_VM_FLAG_SPECIFIC_OVERWRITE* is used.

*vmar_offset* must be 0 if *map_flags* does not have **ZX_VM_FLAG_SPECIFIC** or
**ZX_VM_FLAG_SPECIFIC_OVERWRITE** set.  If neither of those flags are set, then
the mapping will be assigned an offset at random by the kernel (with an
allocator determined by policy set on the target VMAR).

## RETURN VALUE

**vmar_map**() returns **ZX_OK** and the absolute base address of the
mapping (via *mapped_addr*) on success.  In the event of failure, a negative
error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *vmar* or *vmo* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *vmar* or *vmo* is not a VMAR or VMO handle, respectively.

**ZX_ERR_BAD_STATE**  *vmar* refers to a destroyed VMAR.

**ZX_ERR_INVALID_ARGS** *mapped_addr* or *map_flags* are not valid, *vmar_offset* is
non-zero when neither **ZX_VM_FLAG_SPECIFIC** nor
**ZX_VM_FLAG_SPECIFIC_OVERWRITE** are given,
**ZX_VM_FLAG_SPECIFIC_OVERWRITE** and **ZX_VM_FLAG_MAP_RANGE** are both given,
*vmar_offset* and *len* describe an unsatisfiable allocation due to exceeding the region bounds,
*vmar_offset* or *vmo_offset* are not page-aligned,
*vmo_offset* + ROUNDUP(*len*, PAGE_SIZE) overflows, or *len* is 0.

**ZX_ERR_ACCESS_DENIED**  Insufficient privileges to make the requested mapping.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[vmar_allocate](vmar_allocate.md),
[vmar_destroy](vmar_destroy.md),
[vmar_protect](vmar_protect.md),
[vmar_unmap](vmar_unmap.md).
