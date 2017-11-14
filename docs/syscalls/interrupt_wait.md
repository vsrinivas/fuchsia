# zx_interrupt_wait

## NAME

interrupt_wait - wait for an interrupt on an interrupt handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_wait(zx_handle_t handle);
```

## DESCRIPTION

**interrupt_wait**() is a blocking syscall which causes the caller to
wait until either an interrupt occurs for the interrupt vector associated
with *handle* or another thread calls **zx_interrupt_signal()** on *handle*.

## RETURN VALUE

**interrupt_wait**() returns **ZX_OK** when an interrupt has been received,
or **ZX_ERR_CANCELED** if **zx_interrupt_signal()** was called by another
thread on *handle*.

## ERRORS

**ZX_ERR_CANCELED**  *handle* was signalled via **zx_interrupt_signal()**.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_complete](interrupt_complete.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
