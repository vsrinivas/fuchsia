# mx_vcpu_resume

## NAME

vcpu_resume - resume execution of a VCPU

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

mx_status_t mx_vcpu_resume(mx_handle_t vcpu, mx_port_packet_t* packet);
```

## DESCRIPTION

**vcpu_resume**() begins or resumes execution of *vcpu*, and blocks until it has
paused execution. On pause of execution, *packet* is populated with reason for
the pause. After handling the reason, execution may be resumed by calling
**vcpu_resume**() again.

N.B. Execution of a *vcpu* must be resumed on the same thread it was created on.

## RETURN VALUE

**vcpu_resume**() returns MX_OK on success. On failure, an error value is
returned.

## ERRORS

**MX_ERR_ACCESS_DENIED** *vcpu* does not have the *MX_RIGHT_EXECUTE* right.

**MX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**MX_ERR_BAD_STATE** *vcpu* is in a bad state, and can not be executed.

**MX_ERR_CANCELED** *vcpu* execution was canceled while waiting on an event.

**MX_ERR_INTERNAL** There was an error executing *vcpu*.

**MX_ERR_INVALID_ARGS** *packet* is an invalid pointer.

**MX_ERR_NOT_SUPPORTED** An unsupported operation was encountered while
executing *vcpu*.

**MX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
