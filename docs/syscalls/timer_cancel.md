# zx_timer_cancel

## NAME

timer_cancel - cancel a timer

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_timer_cancel(zx_handle_t handle);

```

## DESCRIPTION

**zx_timer_cancel**() cancels a pending timer that was started with
**timer_set**().

Upon success the pending timer is canceled and the **ZX_TIMER_SIGNALED**
signal is de-asserted. If a new pending timer is immediately needed
rather than calling **timer_cancel**() first, call **timer_set**()
with the new deadline.

## RIGHTS

*handle* must have **ZX_RIGHT_WRITE**.

## RETURN VALUE

**zx_timer_cancel**() returns **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_ACCESS_DENIED**  *handle* lacks the right **ZX_RIGHT_WRITE**.

## NOTE

Calling this function before **timer_set**() has no effect.

## SEE ALSO

[timer_create](timer_create.md),
[timer_set](timer_set.md)
