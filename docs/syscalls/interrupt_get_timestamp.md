# zx_interrupt_get_timestamp

## NAME

interrupt_get_timestamp - Get the timestamp for an interrupt

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_get_timestamp(zx_handle_t handle, uint32_t slot,
                                       zx_time_t* out_timestamp);

```

## DESCRIPTION

**interrupt_get_timestamp**() returns the time that an interrupt occured for
the interrupt slot *slot*.
The time is relative to the clock **ZX_CLOCK_MONOTONIC**.
For virtual interrupts, **interrupt_get_timestamp**() returns the timestamp
that was passed to **interrupt_signal**() for the given slot.

## RETURN VALUE

**interrupt_get_timestamp**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_NOT_FOUND** the *slot* parameter is not bound to an interrupt vector or virtual interrupt.

**ZX_ERR_INVALID_ARGS** the *slot* parameter is invalid or the *out_timestamp*
parameter is an invalid pointer.

**ZX_ERR_BAD_STATE** no timestamp is available, either because no interrupt has fired yet
or another thread is currently waiting in **interrupt_wait**().

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
