# zx_vcpu_interrupt

## NAME

vcpu_interrupt - raise an interrupt on a VCPU

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_vcpu_interrupt(zx_handle_t vcpu, uint32_t vector);
```

## DESCRIPTION

**vcpu_interrupt**() raises an interrupt of *vector* on *vcpu*, and may be
called from any thread.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vcpu_interrupt**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *vcpu* does not have the *ZX_RIGHT_SIGNAL* right.

**ZX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**ZX_ERR_OUT_OF_RANGE** *vector* is outside of the range interrupts supported by
the current architecture.

**ZX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
