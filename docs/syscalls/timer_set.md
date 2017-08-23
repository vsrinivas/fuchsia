# mx_timer_set

## NAME

timer_set - start a timer

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_timer_set(mx_handle_t handle, mx_time_t deadline,
                         mx_duration_t slack);

```

## DESCRIPTION

**mx_timer_set**() starts a one-shot timer that will fire when
*deadline* passes. If a previous call to **mx_timer_set**() was
pending, the previous timer is canceled and
*MX_TIMER_SIGNALED* is de-asserted as needed.

The *deadline* parameter specifies a deadline with respect to
**MX_CLOCK_MONOTONIC**. To wait for a relative interval,
use **mx_deadline_after**() returned value in *deadline*.

To fire the timer immediately pass 0 to *deadline*.

When the timer fires it asserts *MX_TIMER_SIGNALED*. To de-assert this
signal call **timer_cancel**() or **timer_set**() again.

The *slack* parameter specifies a range from *deadline* - *slack* to
*deadline* + *slack* during which the timer is allowed to fire. The system
uses this parameter as a hint to coalesce nearby timers.

The precise coalescing behavior is controlled by the *options* parameter
specified when the timer was created. **MX_TIMER_SLACK_EARLY** allows only
firing in the *deadline* - *slack* interval and **MX_TIMER_SLACK_LATE**
allows only firing in the *deadline* + *slack* interval. The default
option value of 0 is **MX_TIMER_SLACK_CENTER** and allows both early and
late firing with an effective interval of *deadline* - *slack* to
*deadline* + *slack*

## RETURN VALUE

**mx_timer_set**() returns **MX_OK** on success.
In the event of failure, a negative error value is returned.


## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_ACCESS_DENIED**  *handle* lacks the right *MX_RIGHT_WRITE*.

## SEE ALSO

[timer_create](timer_create.md),
[timer_cancel](timer_cancel.md),
[deadline_after](deadline_after.md)
