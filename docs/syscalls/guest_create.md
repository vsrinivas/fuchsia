# mx_guest_create

## NAME

guest_create - create a guest

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_guest_create(mx_handle_t resource, uint32_t options,
                            mx_handle_t physmem_vmo, mx_handle_t* out);
```

## DESCRIPTION

**guest_create**() creates a guest, which is a virtual machine that can be run
within the hypervisor, with *physmem_vmo* used to represent the physical memory
of the guest.

To create a guest, a *resource* of *MX_RSRC_KIND_HYPERVISOR* must be supplied.

In order to begin execution within the guest, a VCPU must be created using
**vcpu_create**(), and then run using **vcpu_resume**().

The following rights will be set on the handle *out* by default:

**MX_RIGHT_DUPLICATE** — *out* may be duplicated.

**MX_RIGHT_TRANSFER** — *out* may be transferred over a channel.

**MX_RIGHT_WRITE** - A trap to be may be set using **guest_set_trap**(), or a
VCPU to be created using **vcpu_create**().

## RETURN VALUE

**guest_create**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *resource* is not of *MX_RSRC_KIND_HYPERVISOR*, or
*physmem_vmo* does not have the *MX_RIGHT_READ*, *MX_RIGHT_WRITE*, and
*MX_RIGHT_EXECUTE* rights.

**MX_ERR_BAD_HANDLE** *physmem_vmo* is an invalid handle.

**MX_ERR_INVALID_ARGS** *out* is an invalid pointer, or *options* is nonzero.

**MX_ERR_NO_MEMORY** Temporary failure due to lack of memory.

**MX_ERR_WRONG_TYPE** *resource* is not a handle to a resource, or *physmem_vmo*
is not a handle to a VMO.

## SEE ALSO

[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vmo_create](vmo_create.md),
[vcpu_write_state](vcpu_write_state.md).
