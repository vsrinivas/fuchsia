# zx_interrupt_complete

## NAME

interrupt_complete - clear and unmask an interrupt handle

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_complete(zx_handle_t handle);
```

## DESCRIPTION

**interrupt_complete**() clears the interrupt state and unmasks interrupts
for an interrupt handle.

## RETURN VALUE

**interrupt_complete**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_signal](interrupt_signal.md),
[handle_close](handle_close.md).
