# zx_interrupt_wait

## NAME

interrupt_wait - wait for an interrupt

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_wait(zx_handle_t handle, zx_time_t* out_timestamp);
```

## DESCRIPTION

**interrupt_wait**() is a blocking syscall which causes the caller to
wait until an interrupt is triggered.  It can only be used on interrupt
objects that have not been bound to a port with **interrupt_bind**()

It also, before the waiting begins, will acknowledge the interrupt object,
as if **zx_interrupt_ack**() were called on it.

The wait may be aborted with **zx_interrupt_destroy**() or by closing the handle.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_wait**() returns **ZX_OK** on success, and *out_timestamp*, if
non-NULL, returns the timestamp of when the interrupt was triggered (relative
to **ZX_CLOCK_MONOTONIC**)

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is an invalid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not a handle to an interrupt object.

**ZX_ERR_BAD_STATE** the interrupt object is bound to a port.

**ZX_ERR_ACCESS_DENIED** *handle* lacks **ZX_RIGHT_WAIT**.

**ZX_ERR_CANCELED**  *handle* was closed while waiting or **zx_interrupt_destroy**() was called
on it.

**ZX_ERR_INVALID_ARGS** the *out_timestamp* parameter is an invalid pointer.

## SEE ALSO

[interrupt_ack](interrupt_ack.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_create](interrupt_create.md),
[interrupt_destroy](interrupt_destroy.md),
[interrupt_trigger](interrupt_trigger.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
