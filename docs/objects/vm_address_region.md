# Virtual Memory Address Region

## NAME

vm_address_region - A contiguous region of a virtual memory address space

## SYNOPSIS

Virtual Memory Address Regions (VMARs) represent contiguous parts of a virtual
address space.

## DESCRIPTION

VMARs are used by the kernel and userspace to represent the allocation of an
address space.

Every process starts with a single VMAR (the root VMAR) that spans the entire
address space (see [process_create](../syscalls/process_create.md)).  Each VMAR
can be logically divided up into any number of non-overlapping parts, each
representing a child VMARs, a virtual memory mapping, or a gap.  Child VMARs
are created using [vmar_allocate](../syscalls/vmar_allocate.md).  VM mappings
are created using [vmar_map](../syscalls/vmar_map.md).

VMARs have a hierarchical permission model for allowable mapping permissions.
For example, the root VMAR allows read, write, and executable mapping.  One
could create a child VMAR that only allows read and write mappings, in which
it would be illegal to create a child that allows executable mappings.

When a VMAR is created using vmar_allocate, its parent VMAR retains a reference
to it.  Because of this, if all handles to the child VMAR are closed, the child
and its descendents will remain active in the address space.  In order to
disconnect the child from the address space, [vmar_destroy](../syscalls/vmar_destroy.md)
must be called on a handle to the child.

By default, all allocations of address space are randomized.  At VMAR
creation time, the caller can choose which randomization algorithm is used.
The default allocator attempts to spread allocations widely across the full
width of the VMAR.  The alternate allocator, selected with
*ZX_VM_FLAG_COMPACT*, attempts to keep allocations close together within the
VMAR, but at a random location within the range.  It is recommended to use
the default allocator.

VMARs optionally support a fixed-offset mapping mode (called specific mapping).
This mode can be used to create guard pages or ensure the relative locations of
mappings.  Each VMAR may have the *ZX_VM_FLAG_CAN_MAP_SPECIFIC* permission,
regardless of whether or not its parent VMAR had that permission.

## EXAMPLE

```c
#include <zircon/syscalls.h>

/* Map this VMO into the given VMAR, with |before| bytes of unmapped guard space
   before it and |after| bytes after it.  */
zx_status_t map_with_guard(zx_handle_t vmar, size_t before, size_t after,
                           zx_handle_t vmo, uint64_t vmo_offset,
                           size_t mapping_len, uintptr_t* mapped_addr,
                           zx_handle_t* wrapping_vmar) {

    /* wrap around check elided */
    const size_t child_vmar_size = before + after + mapping_len;
    const uint32_t child_vmar_flags = ZX_VM_FLAG_CAN_MAP_READ |
                                      ZX_VM_FLAG_CAN_MAP_WRITE |
                                      ZX_VM_FLAG_CAN_MAP_SPECIFIC;
    const uint32_t mapping_flags = ZX_VM_FLAG_SPECIFIC |
                                   ZX_VM_FLAG_PERM_READ |
                                   ZX_VM_FLAG_PERM_WRITE;

    uintptr_t child_vmar_addr;
    zx_handle_t child_vmar;
    zx_status_t status = zx_vmar_allocate(vmar, 0, child_vmar_size,
                                          child_vmar_flags, &child_vmar,
                                          &child_vmar_addr);
    if (status != ZX_OK) {
        return status;
    }

    status = zx_vmar_map(child_vmar, before, vmo, vmo_offset, mapping_len,
                         mapping_flags, mapped_addr);
    if (status != ZX_OK) {
        zx_vmar_destroy(child_vmar);
        zx_handle_close(child_vmar);
        return status;
    }

    *wrapping_vmar = child_vmar;
    return ZX_OK;
}
```

## SEE ALSO

+ [vm_object](vm_object.md) - Virtual Memory Objects

## SYSCALLS

+ [vmar_allocate](../syscalls/vmar_allocate.md) - create a new child VMAR
+ [vmar_map](../syscalls/vmar_map.md) - map a VMO into a process
+ [vmar_unmap](../syscalls/vmar_unmap.md) - unmap a memory region from a process
+ [vmar_protect](../syscalls/vmar_protect.md) - adjust memory access permissions
+ [vmar_destroy](../syscalls/vmar_destroy.md) - destroy a VMAR and all of its children
