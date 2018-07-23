# zx_guest_create

## NAME

guest_create - create a guest

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_guest_create(zx_handle_t resource, uint32_t options,
                            zx_handle_t physmem_vmo, zx_handle_t* out);
```

## DESCRIPTION

**guest_create**() creates a guest, which is a virtual machine that can be run
within the hypervisor, with *physmem_vmo* used to represent the physical memory
of the guest.

To create a guest, a *resource* of *ZX_RSRC_KIND_HYPERVISOR* must be supplied.

In order to begin execution within the guest, a VCPU must be created using
**vcpu_create**(), and then run using **vcpu_resume**().

The following rights will be set on the handle *out* by default:

**ZX_RIGHT_DUPLICATE** — *out* may be duplicated.

**ZX_RIGHT_TRANSFER** — *out* may be transferred over a channel.

**ZX_RIGHT_WRITE** - A trap to be may be set using **guest_set_trap**(), or a
VCPU to be created using **vcpu_create**().

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**guest_create**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *resource* is not of *ZX_RSRC_KIND_HYPERVISOR*, or
*physmem_vmo* does not have the *ZX_RIGHT_READ*, *ZX_RIGHT_WRITE*, and
*ZX_RIGHT_EXECUTE* rights.

**ZX_ERR_BAD_HANDLE** *physmem_vmo* is an invalid handle.

**ZX_ERR_INVALID_ARGS** *out* is an invalid pointer, or *options* is nonzero.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_WRONG_TYPE** *resource* is not a handle to a resource, or *physmem_vmo*
is not a handle to a VMO.

## SEE ALSO

[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vmo_create](vmo_create.md),
[vcpu_write_state](vcpu_write_state.md).
