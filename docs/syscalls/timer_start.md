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

**mx_timer_start**() starts a timer that will fire when *deadline* passes and
optionally continue firing afterwards when each *period* has elapsed.

The *deadline* parameter specifies a deadline with respect to
**MX_CLOCK_MONOTONIC** and cannot be zero. To wait for a relative interval,
use **mx_deadline_after**() returned value in *deadline*.

If *period* is zero, the timer is one-shot and when the timer fires it
asserts *MX_TIMER_SIGNALED*. To de-assert this signal call **timer_cancel**()
or **timer_start**() again.

If *period* is at least *MX_TIMER_MIN_PERIOD* the timer will fire
when *deadline* passes, then at *dealine* + *period* and so on. In this
mode the *MX_TIMER_SIGNALED* signal is not asserted but strobed.
This means that it can satisfy an existing wait operation or generate a
port signal packet, but it cannot be reliably inspected.

The *slack* parameter should be set to zero.

## RETURN VALUE

**mx_timer_start**() returns **NO_ERROR** on success.
In the event of failure, a negative error value is returned.


## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *handle* lacks the right *MX_RIGHT_WRITE*.

**ERR_INVALID_ARGS**  *deadline* is less than *MX_TIMER_MIN_DEADLINE*

**ERR_NOT_SUPPORTED**  *period* is less than *MX_TIMER_MIN_PERIOD*.

## NOTE

*slack* is ignored at the moment. It will be used to coalesce timers.


## SEE ALSO

[timer_create](timer_create.md),
[deadline_after](deadline_after.md)
