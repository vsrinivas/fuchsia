# zx_interrupt_wait

## NAME

interrupt_wait - wait for an interrupt on an interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_wait(zx_handle_t handle, uint64_t* out_slots);
```

## DESCRIPTION

**interrupt_wait**() is a blocking syscall which causes the caller to
wait until either an interrupt occurs for an interrupt vector bound to
*handle*, or another thread calls **zx_interrupt_signal()**
on a slot bound to the interrupt object.

Upon successful return, the *out_slots* parameter returns a bitmask
of the slots that have been signalled.

For level-triggered hardware interrupts that have been signaled,
**interrupt_wait()** will mask the interrupt before returning
and unmask the interrupt when it is called again the next time.
For edge-triggered interrupts, the interrupt remains unmasked.

It is not safe to call **interrupt_wait**() from multiple threads simultaneously.

## RETURN VALUE

**interrupt_wait**() returns **ZX_OK** when an interrupt has been received
or a virtual interrupt was signaled via the **interrupt_signal**() syscall.

## ERRORS

**ZX_ERR_CANCELED**  *handle* was closed while blocked in **zx_interrupt_wait()**.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_INVALID_ARGS** the *out_slots* parameter is an invalid pointer.

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_get_timestamp](interrupt_get_timestamp.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
