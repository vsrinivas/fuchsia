# Virtual Memory Address Region

## NAME

vm_address_region - A contiguous region of a virtual memory address space

## SYNOPSIS

Virtual Memory Address Regions (VMARs) are used to represent contiguous parts of
an address space.  Every process starts with a single VMAR that spans the entire
address space.  Each VMAR can be logically divided up into any number of
non-overlapping parts, each representing a child VMARs, a virtual memory mapping,
or a gap.

## DESCRIPTION

TODO

## SEE ALSO

[mx_vmar_allocate](../syscalls/vmar_allocate.md),
[mx_vmar_destroy](../syscalls/vmar_destroy.md),
[mx_vmar_map](../syscalls/vmar_map.md),
[mx_vmar_protect](../syscalls/vmar_protect.md),
[mx_vmar_unmap](../syscalls/vmar_unmap.md),
