# zx_interrupt_create

## NAME

interrupt_create - create an interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_create(zx_handle_t src_obj, uint32_t src_num,
                                uint32_t options, zx_handle_t* out_handle);

```

## DESCRIPTION

**interrupt_create**() creates an interrupt object which represents a physical
or virtual interrupt.

If *options* is **ZX_INTERRUPT_VIRTUAL**, *src_obj* and *src_num* are ignored and
a virtual interrupt is returned.

Otherwise *src_obj* must be a suitable resource for creating platform interrupts
or a PCI object, and *src_num* is the associated interrupt number.  This restricts
the creation of interrupts to the internals of the DDK (driver development kit).
Physical interrupts are obtained by drivers through various DDK APIs.

Physical interrupts honor the options **ZX_INTERRUPT_EDGE_LOW**, **ZX_INTERRUPT_EDGE_HIGH**,
**ZX_INTERRUPT_LEVEL_LOW**, **ZX_INTERRUPT_LEVEL_HIGH**, and **ZX_INTERRUPT_REMAP_IRQ**.

The handles will have *ZX_RIGHT_INSPECT*, *ZX_RIGHT_DUPLICATE*, *ZX_RIGHT_TRANSFER*
(allowing them to be sent to another process via channel write), *ZX_RIGHT_READ*,
*ZX_RIGHT_WRITE* (required for **interrupt_ack**()), *ZX_RIGHT_WAIT* (required for
**interrupt_wait**(), and *ZX_RIGHT_SIGNAL* (required for **interrupt_trigger**()).

Interrupts are said to be "triggered" when the underlying physical interrupt occurs
or when **interrupt_trigger**() is called on a virtual interrupt.  A triggered interrupt,
when bound to a port with **interrupt_bind**(), causes a packet to be delivered to the port.

If not bound to a port, an interrupt object may be waited on with **interrupt_wait**().

Interrupts cannot be waited on with the **object_wait_** family of calls.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**interrupt_create**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** the *src_obj* handle is invalid (if this is not a virtual interrupt)

**ZX_ERR_WRONG_TYPE** the *src_obj* handle is not of an appropriate type to create an interrupt.

**ZX_ERR_ACCESS_DENIED** the *src_obj* handle does not allow this operation.

**ZX_ERR_INVALID_ARGS** *options* contains invalid flags or the *out_handle*
parameter is an invalid pointer.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[interrupt_ack](interrupt_ack.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_destroy](interrupt_destroy.md),
[interrupt_wait](interrupt_wait.md),
[port_wait](port_wait.md),
[handle_close](handle_close.md).
