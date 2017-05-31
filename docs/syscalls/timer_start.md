# mx_timer_start

## NAME

timer_start - start a timer

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_timer_start(mx_handle_t handle, mx_time_t deadline, mx_duration_t period,
                           mx_duration_t slack);

```

## DESCRIPTION

**mx_timer_start**() starts a timer with will fire when *deadline* passes. Currently
periodic timers are not supported so the caller should pass 0 for *period* and *slack*.

When the timer fires it asserts MX_TIMER_SIGNALED. To de-assert this signal call
**timer_cancel**() or **timer_start**() again.

The *deadline* parameter specifies a deadline with respect to **MX_CLOCK_MONOTONIC**
and cannot be zero. To wait for a relative interval use **mx_deadline_after**()
returned value in *deadline*.

## RETURN VALUE

**mx_timer_start**() returns **NO_ERROR** on success.
In the event of failure, a negative error value is returned.


## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *handle* lacks the right *MX_RIGHT_WRITE*.

**ERR_INVALID_ARGS**  *deadline* is zero or *period* is non-zero.

## NOTE

*slack* is ignored at the moment. It will be used to coalesce timers.


## SEE ALSO

[timer_create](timer_create.md),
[deadline_after](deadline_after.md)
