# Virtual Memory Object

## NAME

vm\_object - Virtual memory containers

## SYNOPSIS

A Virtual Memory Object (VMO) represents a contiguous region of virtual memory
that may be mapped into multiple address spaces.

## DESCRIPTION

VMOs are used in by the kernel and userspace to represent both paged and physical memory.
They are the standard method of sharing memory between processes, as well as between the kernel and
userspace.

VMOs are created with [vmo_create](../syscalls/vmo_create.md) and basic I/O can be
performed on them with [vmo_read](../syscalls/vmo_read.md) and [vmo_write](../syscalls/vmo_write.md). A VMO's size may be set using [vmo_set_size](../syscalls/vmo_set_size.md). Conversely, [vmo_get_size](../syscalls/vmo_get_size.md) will retrieve a VMO's current size.

Pages are committed (allocated) for VMOs on demand through [vmo_read](../syscalls/vmo_read.md), [vmo_write](../syscalls/vmo_write.md), or by mapping the
VMO using [vmar_map](../syscalls/vmar_map.md). Pages can be commited and decommited from a VMO manually by calling
[vmo_op_range](../syscalls/vmo_op_range.md) with the *MX_VMO_OP_COMMIT* and *MX_VMO_OP_DECOMMIT*
operations, but this should be considered a low level operation. [vmo_op_range](../syscalls/vmo_op_range.md) can also be used for cache and locking operations against pages a VMO holds.

Processes with special purpose use cases involving cache policy can use
[vmo_set_cache_policy](../syscalls/vmo_set_cache_policy.md) to change the policy of a given VMO.
This use case typically applies to device drivers.

## SYSCALLS

+ [vmo_create](../syscalls/vmo_create.md) - create a new vmo
+ [vmo_read](../syscalls/vmo_read.md) - read from a vmo
+ [vmo_write](../syscalls/vmo_write.md) - write to a vmo
+ [vmo_get_size](../syscalls/vmo_get_size.md) - obtain the size of a vmo
+ [vmo_set_size](../syscalls/vmo_set_size.md) - adjust the size of a vmo
+ [vmo_op_range](../syscalls/vmo_op_range.md) - perform an operation on a range of a vmo
+ [vmo_set_cache_policy](../syscalls/vmo_set_cache_policy.md)

<br>

+ [vmar_map](../syscalls/vmar_map.md) - map a VMO into a process
+ [vmar_unmap](../syscalls/vmar_unmap.md) - unmap memory from a process
