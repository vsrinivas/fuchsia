# zx_interrupt_ack

## NAME

interrupt_ack - Acknowledge an interrupt and re-arm it.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_ack(zx_handle_t handle);

```

## DESCRIPTION

**interrupt_ack**() acknowledges an interrupt object, causing it to be eligible
to trigger again (and delivering a packet to the port it is bound to).

If the interrupt object is a physical interrupt, if it is a level interrupt and
still asserted, or is an edge interrupt that has been asserted since it last
triggered, the interrupt will trigger immediately, delivering a packet to the
port it is bound to.

Virtual interrupts behave as edge interrupts.

This syscall only operates on interrupts which are bound to a port.  Interrupts
being waited upon with **interrupt_wait**() do not need to be re-armed with this
call -- it happens automatically when **interrupt_wait**() is called.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_ack**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is an invalid handle.

**ZX_ERR_WRONG_TYPE** *handle* is not an interrupt object.

**ZX_ERR_BAD_STATE** *handle* is not bound to a port.

**ZX_ERR_CANCELED**  **zx_interrupt_destroy**() was called on *handle*.

**ZX_ERR_ACCESS_DENIED** *handle* lacks **ZX_RIGHT_WRITE**.

## SEE ALSO

[interrupt_bind](interrupt_bind.md),
[interrupt_create](interrupt_create.md),
[interrupt_destroy](interrupt_destroy.md),
[interrupt_trigger](interrupt_trigger.md),
[interrupt_wait](interrupt_wait.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
