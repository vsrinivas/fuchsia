# mx_vcpu_interrupt

## NAME

vcpu_interrupt - raise an interrupt on a VCPU

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_vcpu_interrupt(mx_handle_t vcpu, uint32_t vector);
```

## DESCRIPTION

**vcpu_interrupt**() raises an interrupt of *vector* on *vcpu*, and may be
called from any thread.

## RETURN VALUE

**vcpu_interrupt**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *vcpu* does not have the *MX_RIGHT_SIGNAL* right.

**MX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**MX_ERR_OUT_OF_RANGE** *vector* is outside of the range interrupts supported by
the current architecture.

**MX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
