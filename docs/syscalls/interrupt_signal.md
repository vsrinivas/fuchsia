# zx_interrupt_signal

## NAME

interrupt_signal - signals a virtual interrupt on an interrupt object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_interrupt_signal(zx_handle_t handle, uint32_t slot, zx_time_t timestamp);
```

## DESCRIPTION

**interrupt_signal**() is used to signal a virtual interrupt on an interrupt object.
The *slot* parameter must have been bound to the interrupt object using **interrupt_bind**()
with the **ZX_INTERRUPT_VIRTUAL** flag set, or be the special slot **ZX_INTERRUPT_SLOT_USER**.

**interrupt_signal**() will unblock a call to **interrupt_wait**() on the handle
with the *slot* bit set in the **interrupt_wait**() *out_slots* parameter.
After **interrupt_wait**() returns, the *timestamp* value can retrieved with
**interrupt_get_timestamp**().

## RETURN VALUE

**interrupt_signal**() returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* is not a valid handle.

**ZX_ERR_BAD_STATE** *slot* was not bound with the **ZX_INTERRUPT_VIRTUAL** flag set

**ZX_ERR_INVALID_ARGS** the *slot* parameter is invalid.

**ZX_ERR_NOT_FOUND** if *slot* was not bound with **interrupt_bind**()

## SEE ALSO

[interrupt_create](interrupt_create.md),
[interrupt_bind](interrupt_bind.md),
[interrupt_wait](interrupt_wait.md),
[interrupt_get_timestamp](interrupt_get_timestamp.md),
[handle_close](handle_close.md).
