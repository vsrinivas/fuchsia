# zx_interrupt_signal

## NAME

interrupt_signal - unblocks the interupt_wait syscall

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_signal(zx_handle_t handle);
```

## DESCRIPTION

**interrupt_signal**() causes any thread blocked in **zx_interrupt_wait**
for the same interrupt handle as *handle* to to unblock and return **ZX_ERR_CANCELED**.
This can be used to unblock an interrupt thread so it can exit, when shutting down a driver.

## RETURN VALUE

**interrupt_signal**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_complete](interrupt_complete.md),
[handle_close](handle_close.md).
