# mx_timer_cancel

## NAME

timer_cancel - cancel a timer

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_timer_cancel(mx_handle_t handle);

```

## DESCRIPTION

**mx_timer_cancel**() cancels a pending timer that was started with
**timer_set**().

Upon success the pending timer is canceled and the MX_TIMER_SIGNALED
signal is de-asserted. If a new pending timer is immediately needed
rather than calling **timer_cancel**() first, call **timer_set**()
with the new deadline.

## RETURN VALUE

**mx_timer_cancel**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_ACCESS_DENIED**  *handle* lacks the right *MX_RIGHT_WRITE*.

## NOTE

Calling this function before **timer_set**() has no effect.

## SEE ALSO

[timer_create](timer_create.md),
[timer_set](timer_set.md)
