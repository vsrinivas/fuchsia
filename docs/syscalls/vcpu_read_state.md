# mx_vcpu_read_state

## NAME

vcpu_read_state - read the state of a VCPU

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

mx_status_t mx_vcpu_read_state(mx_handle_t vcpu, uint32_t kind, void* buffer,
                               uint32_t len);
```

## DESCRIPTION

**vcpu_read_state**() reads the state of *vcpu* as specified by *kind* into
*buffer*. It is only valid to read the state of *vcpu* when execution has been
paused.

## RETURN VALUE

**vcpu_read_state**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *vcpu* does not have the *MX_RIGHT_READ* right.

**MX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**MX_ERR_BAD_STATE** *vcpu* is in a bad state, and state can not be read.

**MX_ERR_INVALID_ARGS** *kind* does not name a known VCPU state, *buffer* is an
invalid pointer, or *len* does not match the expected size of *kind*.

**MX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_write_state](vcpu_write_state.md).
