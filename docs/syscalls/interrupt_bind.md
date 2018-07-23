# zx_interrupt_bind

## NAME

interrupt_bind - Bind an interrupt object to a port

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_bind(zx_handle_t inth, zx_handle_t porth,
                              uint64_t key, uint32_t options);

```

## DESCRIPTION

**interrupt_bind**() binds an interrupt object to a port.

An interrupt object may only be bound to a single port and may only be bound once.

When a bound interrupt object is triggered, a **ZX_PKT_TYPE_INTERRUPT** packet will
be delivered to the port it is bound to, with the timestamp (relative to **ZX_CLOCK_MONOTONIC**)
of when the interrupt was triggered in the `zx_packet_interrupt_t`.  The *key* used
when binding the interrupt will be present in the `key` field of the `zx_port_packet_t`.

Before another packet may be delivered, the bound interrupt must be re-armed using the
**interrupt_ack**() syscall.  This is (in almost all cases) best done after the interrupt
packet has been fully processed.  Especially in the case of multiple threads reading
packets from a port, if the processing thread re-arms the interrupt and it has triggered,
a packet will immediately be delivered to a waiting thread.

Interrupt packets are delivered via a dedicated queue on ports and are higher priority
than non-interrupt packets.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_bind**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *inth* or *porth* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *inth* is not an interrupt object or *porth* is not a port object.

**ZX_ERR_CANCELED**  **zx_interrupt_destroy**() was called on *inth*.

**ZX_ERR_BAD_STATE**  A thread is waiting on the interrupt using **zx_interrupt_wait**()

**ZX_ERR_ACCESS_DENIED** the *inth* handle lacks **ZX_RIGHT_READ** or the *porth* handle
lacks **ZX_RIGHT_WRITE**

**ZX_ERR_ALREADY_BOUND** this interrupt object is already bound.

**ZX_ERR_INVALID_ARGS** *options* contains a non-zero value.

## SEE ALSO

[interrupt_ack](interrupt_ack.md),
[interrupt_create](interrupt_create.md),
[interrupt_destroy](interrupt_destroy.md),
[interrupt_trigger](interrupt_trigger.md),
[interrupt_wait](interrupt_wait.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
