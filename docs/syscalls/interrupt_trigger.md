# zx_interrupt_trigger

## NAME

interrupt_trigger - triggers a virtual interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_trigger(zx_handle_t handle, uint32_t options, zx_time_t timestamp);
```

## DESCRIPTION

**interrupt_trigger**() is used to trigger a virtual interrupt interrupt object,
causing an interrupt message packet to arrive on the bound port, if it is bound
to a port, or **interrupt_wait**() to return if it is waiting on this interrupt.

*options* must be zero.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_signal**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is an invalid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not an interrupt object.

**ZX_ERR_BAD_STATE** *handle* is not a virtual interrupt.

**ZX_ERR_CANCELED**  **zx_interrupt_destroy**() was called on *handle*.

**ZX_ERR_ACCESS_DENIED** *handle* lacks **ZX_RIGHT_SIGNAL**.

**ZX_ERR_INVALID_ARGS** *options* is non-zero.

## SEE ALSO

[interrupt_ack](interrupt_ack.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_create](interrupt_create.md),
[interrupt_destroy](interrupt_destroy.md),
[interrupt_wait](interrupt_wait.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
