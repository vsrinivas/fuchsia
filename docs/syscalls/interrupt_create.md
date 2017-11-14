# zx_interrupt_create

## NAME

interrupt_create - create an interrupt handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_create(zx_handle_t resource, uint32_t vector,
                                uint32_t options, zx_handle_t* out_handle);

```

## DESCRIPTION

**interrupt_create**() creates a handle for drivers to use to wait for
hardware interrupts.

The parameter *resource* is a resource handle used to control access to this
syscall. *resource* must be of type *ZX_RSRC_KIND_IRQ* or be the root resource.

The parameter *vector* is the interrupt vector number for the interrupt.

The parameter *options* is a bitfield containing zero or more of the following flags:

**ZX_INTERRUPT_REMAP_IRQ** - remap interrupt vector if necessary

**ZX_INTERRUPT_MODE_EDGE_LOW** - edge trigger interrupt on low state

**ZX_INTERRUPT_MODE_EDGE_HIGH** - edge trigger interrupt on high state

**ZX_INTERRUPT_MODE_LEVEL_LOW** - level trigger interrupt on low state

**ZX_INTERRUPT_MODE_LEVEL_HIGH** - level trigger interrupt on high state

**ZX_INTERRUPT_MODE_DEFAULT** - same as ZX_INTERRUPT_MODE_EDGE_LOW

An interrupt handle is returned in the *out_handle* parameter on success.

The handles will have *ZX_RIGHT_TRANSFER* (allowing them to be sent
to another process via channel write), as well as *ZX_RIGHT_READ* and *ZX_RIGHT_WRITE*.
In particular, interrupt handles do not have *ZX_RIGHT_DUPLICATE*.

## RETURN VALUE

**interrupt_create**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_ACCESS_DENIED** the *resource* handle does not allow this operation.

**ZX_ERR_INVALID_ARGS**  *vector* contains an invalid interrupt number,
*options* contains invalid flags or the *out_handle* parameter is an invalid pointer.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[interrupt_wait](interrupt_wait.md),
[interrupt_complete](interrupt_complete.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
