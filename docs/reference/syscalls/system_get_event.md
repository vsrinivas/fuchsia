# zx_system_get_event

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Retrieve a handle to a system event.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_system_get_event(zx_handle_t root_job,
                                uint32_t kind,
                                zx_handle_t* event);
```

## DESCRIPTION

*root_job* must be a handle to the root job of the system with the
MANAGE_PROCESS right.

The only valid value for *kind* is ZX_SYSTEM_EVENT_LOW_MEMORY.

When *kind* is ZX_SYSTEM_EVENT_LOW_MEMORY, an *event* will be returned that will
assert ZX_EVENT_SIGNALED when the system is nearing an out-of-memory situation.
A process that is waiting on this event must quickly perform any important
shutdown work. It is unspecified how much memory is available at the time this
event is signaled, and unspecified how long the waiting process has to act
before the kernel starts terminating processes or starting a full system reboot.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

None.

## RETURN VALUE

`zx_system_get_event()` returns ZX_OK on success, and *event* will be a valid
handle, or an error code from below on failure.

## ERRORS

**ZX_ERR_ACCESS_DENIED** The calling process' policy was invalid, the handle
*root_job* did not have ZX_RIGHT_MANAGE_PROCESS rights, *root_job* was not the
root job of the system.

**ZX_ERR_INVALID_ARGS** *kind* was not ZX_SYSTEM_EVENT_LOW_MEMORY.
