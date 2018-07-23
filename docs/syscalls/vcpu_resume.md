# zx_vcpu_resume

## NAME

vcpu_resume - resume execution of a VCPU

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

zx_status_t zx_vcpu_resume(zx_handle_t vcpu, zx_port_packet_t* packet);
```

## DESCRIPTION

**vcpu_resume**() begins or resumes execution of *vcpu*, and blocks until it has
paused execution. On pause of execution, *packet* is populated with reason for
the pause. After handling the reason, execution may be resumed by calling
**vcpu_resume**() again.

N.B. Execution of a *vcpu* must be resumed on the same thread it was created on.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**vcpu_resume**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *vcpu* does not have the *ZX_RIGHT_EXECUTE* right.

**ZX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**ZX_ERR_BAD_STATE** *vcpu* is in a bad state, and can not be executed.

**ZX_ERR_CANCELED** *vcpu* execution was canceled while waiting on an event.

**ZX_ERR_INTERNAL** There was an error executing *vcpu*.

**ZX_ERR_INVALID_ARGS** *packet* is an invalid pointer.

**ZX_ERR_NOT_SUPPORTED** An unsupported operation was encountered while
executing *vcpu*.

**ZX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md),
[vcpu_write_state](vcpu_write_state.md).
