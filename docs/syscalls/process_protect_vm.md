# mx_process_protect_vm

## NAME

process_protect_vm - set protection of a memory mapping

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_process_protect_vm(mx_handle_t proc_handle,
                                  uintptr_t address, mx_size_t len,
                                  uint32_t prot);
```

## DESCRIPTION

**process_protect_vm**() alters the access protections for the memory region
in which *address* is located. The *prot* argument should be either
PROT_NONE or the bitwise OR of one or more of PROT_READ, PROT_WRITE, and
PROT_EXEC.

Behavior is undefined if *address* was not mapped via the **process_vm_map**()
function.

## RETURN VALUE

**process_protect_vm**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *proc_handle* isn't a valid process handle, or
*address* is not from a valid mapped region, or *prot* is an unsupported
combination of flags (e.g., PROT_WRITE but not PROT_READ).

**ERR_ACCESS_DENIED**  *proc_handle* does not have **MX_RIGHT_WRITE**.

## NOTES

Currently the *len* parameter is ignored, and the entire region that was
mapped is altered.

PROT_NONE is not yet supported.

## SEE ALSO

[process_map_vm](process_map_vm.md).
[process_unmap_vm](process_unmap_vm.md).
