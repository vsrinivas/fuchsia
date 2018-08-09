# zx_guest_create

## NAME

guest_create - create a guest

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_guest_create(zx_handle_t resource, uint32_t options,
                            zx_handle_t* guest_handle,
                            zx_handle_t* vmar_handle);
```

## DESCRIPTION

**guest_create**() creates a guest, which is a virtual machine that can be run
within the hypervisor, with *vmar_handle* used to represent the physical address
space of the guest.

To create a guest, a *resource* of *ZX_RSRC_KIND_HYPERVISOR* must be supplied.

In order to begin execution within the guest, a VMO should be mapped into
*vmar_handle* using **vmar_map**(), and a VCPU must be created using
**vcpu_create**(), and then run using **vcpu_resume**().

Additionally, a VMO should be mapped into *vmar_handle* to provide a guest with
physical memory.

The following rights will be set on the handle *guest_handle* by default:

**ZX_RIGHT_TRANSFER** — *guest_handle* may be transferred over a channel.

**ZX_RIGHT_DUPLICATE** — *guest_handle* may be duplicated.

**ZX_RIGHT_WRITE** - A trap to be may be set using **guest_set_trap**(), or a
VCPU to be created using **vcpu_create**().

See [vmar_create](vmar_create.md) for the set of rights applied to
*vmar_handle*.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**guest_create**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *resource* is not of *ZX_RSRC_KIND_HYPERVISOR*.

**ZX_ERR_INVALID_ARGS** *guest_handle* or *vmar_handle* is an invalid pointer,
or *options* is nonzero.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_WRONG_TYPE** *resource* is not a handle to a resource.

## SEE ALSO

[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md),
[vmar_map](vmar_map.md),
[vmo_create](vmo_create.md).
