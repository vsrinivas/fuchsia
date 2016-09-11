# mx_process_map_vm

## NAME

mx_process_map_vm - add a memory mapping

## SYNOPSIS

## DESCRIPTION

The mapping retains a reference to the underlying virtual memory object, which
means closing the VMO handle does not remove the mapping added by this function.

## RETURN VALUE

## ERRORS

## NOTES

A virtual memory object can be larger than the address space, which means you
should check for overflow before converting the **uint64_t** size of the VMO to
mx_process_map_vm's **mx_size_t** *len* parameter.

## SEE ALSO

[process_protect_vm](process_protect_vm.md).
[process_unmap_vm](process_unmap_vm.md).
