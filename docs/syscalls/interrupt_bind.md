# zx_interrupt_bind

## NAME

interrupt_bind - Bind an interrupt vector to interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_bind(zx_handle_t handle, uint32_t slot, zx_handle_t resource,
                              uint32_t vector, uint32_t options);

```

## DESCRIPTION

**interrupt_bind**() binds an interrupt vector to an interrupt object.
The *slot* parameter is a number to be associated with the interrupt vector for use by the
**interrupt_wait**(), **interrupt_get_timestamp**() and **interrupt_signal**() syscalls.
*slot* must be less than **ZX_INTERRUPT_MAX_SLOTS**, and cannot be **ZX_INTERRUPT_SLOT_USER**,
which is implicitly bound by the system to all interrupt objects.

After calling this, **interrupt_wait**() can be used to wait for interrupts
on the specified interrupt vector.

**interrupt_bind**() can also be used to bind a slot to a virtual interrupt.
To bind a virtual interrupt, set the *options* flag to **ZX_INTERRUPT_VIRTUAL**.
In this case the *resource* and *vector* parameters are ignored.

PCI interrupts are bound to the system for the PCI device's interrupt vector.
The PCI device interrupt is signaled on **ZX_PCI_INTERRUPT_SLOT**.
Interrupt handles referring to a PCI interrupt may only be additionally bound to virtual interrupts.

The parameter *vector* is the interrupt vector number for the interrupt.

The parameter *options* is a bitfield containing zero or more of the following flags:

**ZX_INTERRUPT_REMAP_IRQ** - remap interrupt vector if necessary

**ZX_INTERRUPT_MODE_EDGE_LOW** - edge trigger interrupt on low state

**ZX_INTERRUPT_MODE_EDGE_HIGH** - edge trigger interrupt on high state

**ZX_INTERRUPT_MODE_LEVEL_LOW** - level trigger interrupt on low state

**ZX_INTERRUPT_MODE_LEVEL_HIGH** - level trigger interrupt on high state

**ZX_INTERRUPT_MODE_DEFAULT** - interrupt triggering will be set to the default mode for the platform

**ZX_INTERRUPT_VIRTUAL** - binds the slot to be used for a virtual interrupt

## RETURN VALUE

**interrupt_bind**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_ACCESS_DENIED** the *resource* handle does not allow this operation.

**ZX_ERR_ALREADY_BOUND** *slot* has already been bound to this handle
or the interrupt vector *vector* has already been bound to an interrupt object.

**ZX_ERR_INVALID_ARGS** *options* contains invalid flags or the *slot* parameter is invalid.

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_get_timestamp](interrupt_get_timestamp.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
