# zx_vcpu_write_state

## NAME

vcpu_write_state - write the state of a VCPU

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

zx_status_t zx_vcpu_write_state(zx_handle_t vcpu, uint32_t kind,
                                const void* buffer, uint32_t len);
```

## DESCRIPTION

**vcpu_write_state**() writes the state of *vcpu* as specified by *kind* from
*buffer*. It is only valid to write the state of *vcpu* when execution has been
paused.

*kind* may be *ZX_VCPU_STATE* or *ZX_VCPU_IO*.

## RETURN VALUE

**vcpu_write_state**() returns ZX_OK on success. On failure, an error value is
returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** *vcpu* does not have the *ZX_RIGHT_WRITE* right.

**ZX_ERR_BAD_HANDLE** *vcpu* is an invalid handle.

**ZX_ERR_BAD_STATE** *vcpu* is in a bad state, and state can not be written.

**ZX_ERR_INVALID_ARGS** *kind* does not name a known VCPU state, *buffer* is an
invalid pointer, or *len* does not match the expected size of *kind*.

**ZX_ERR_WRONG_TYPE** *vcpu* is not a handle to a VCPU.

## SEE ALSO

[guest_create](guest_create.md),
[guest_set_trap](guest_set_trap.md),
[vcpu_create](vcpu_create.md),
[vcpu_resume](vcpu_resume.md),
[vcpu_interrupt](vcpu_interrupt.md),
[vcpu_read_state](vcpu_read_state.md).
