# mx_vmar_allocate

## NAME

vmar_allocate - allocate a new subregion

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vmar_allocate(mx_handle_t parent_vmar_handle,
                             size_t offset, size_t size, uint32_t map_flags,
                             mx_handle_t* child_vmar, uintptr_t* child_addr)
```

## DESCRIPTION

Creates a new VMAR within the one specified by *parent_vmar_handle*.

*map_flags* is a bit vector of the following flags:
- **MX_VM_FLAG_COMPACT**  A hint to the kernel that allocations and mappings
  within the newly created subregion should be kept close together.   See the
  NOTES section below for discussion.
- **MX_VM_FLAG_SPECIFIC**  Use the *offset* to place the mapping, invalid if
  vmar does not have the **MX_VM_FLAG_CAN_MAP_SPECIFIC** permission.  *offset*
  is an offset relative to the base address of the parent region.  It is an error
  to specify an address range that overlaps with another VMAR or mapping.
- **MX_VM_FLAG_CAN_MAP_SPECIFIC**  The new VMAR can have subregions/mappings
  created with **MX_VM_FLAG_SPECIFIC**.  It is NOT an error if the parent does
  not have *MX_VM_FLAG_CAN_MAP_SPECIFIC* permissions.
- **MX_VM_FLAG_CAN_MAP_READ**  The new VMAR can contain readable mappings.
  It is an error if the parent does not have *MX_VM_FLAG_CAN_MAP_READ* permissions.
- **MX_VM_FLAG_CAN_MAP_WRITE**  The new VMAR can contain writable mappings.
  It is an error if the parent does not have *MX_VM_FLAG_CAN_MAP_WRITE* permissions.
- **MX_VM_FLAG_CAN_MAP_EXECUTE**  The new VMAR can contain executable mappings.
  It is an error if the parent does not have *MX_VM_FLAG_CAN_MAP_EXECUTE* permissions.

*offset* must be 0 if *map_flags* does not have **MX_VM_FLAG_SPECIFIC** set.

## RETURN VALUE

**vmar_allocate**() returns **MX_OK**, the absolute base address of the
subregion (via *child_addr*), and a handle to the new subregion (via
*child_vmar*) on success.  In the event of failure, a negative error value is
returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *parent_vmar_handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *parent_vmar_handle* is not a VMAR handle.

**MX_ERR_BAD_STATE**  *parent_vmar_handle* refers to a destroyed VMAR.

**MX_ERR_INVALID_ARGS**  *child_vmar* or *child_addr* are not valid, *offset* is
non-zero when *MX_VM_FLAG_SPECIFIC* is not given, *offset* and *size* describe
an unsatisfiable allocation due to exceeding the region bounds, *offset*
or *size* is not page-aligned, or *size* is 0.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**MX_ERR_ACCESS_DENIED**  Insufficient privileges to make the requested allocation.

## NOTES

### The COMPACT flag

The kernel interprets this flag as a request to reduce sprawl in allocations.
While this does not necessitate reducing the absolute entropy of the allocated
addresses, there will potentially be a very high correlation between allocations.
This is a trade-off that the developer can make to increase locality of
allocations and reduce the number of page tables necessary, if they are willing
to have certain addresses be more correlated.

## SEE ALSO

[vmar_destroy](vmar_destroy.md),
[vmar_map](vmar_map.md),
[vmar_protect](vmar_protect.md),
[vmar_unmap](vmar_unmap.md).
