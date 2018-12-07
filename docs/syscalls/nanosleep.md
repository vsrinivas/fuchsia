# zx_nanosleep

## NAME

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

nanosleep - high resolution sleep

## SYNOPSIS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_nanosleep(zx_time_t deadline);
```

## DESCRIPTION

**nanosleep**() suspends the calling thread execution until *deadline* passes on
**ZX_CLOCK_MONOTONIC**. A *deadline* value less than or equal to **0**
immediately yields the thread.

To sleep for a duration, use [**zx_deadline_after**](deadline_after.md) and the
**ZX_\<time-unit\>** helpers:

```
#include <zircon/syscalls.h> // zx_deadline_after, zx_nanosleep
#include <zircon/types.h> // ZX_MSEC et al.

// Sleep 50 milliseconds
zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
```

The nanosleep duration has 10% late slack with a minimum slack time of 1
microsecond and a maximum slack time of 1 second. This slack time is the amount
of additional time that the thread might sleep before being rescheduled. For
example, a thread that requests a nanosleep with a duration of 1 second will
have a slack time of .1 second. This means that the thread will sleep anywhere
between 1 and 1.1 seconds. See [timer_set](timer_set.md) for a more in-depth
description of slack.

The slack is included so the operating system can coalesce sleeps for
performance and energy reasons.  If more precise timing is needed, it is
recommended to use a timer.

## RIGHTS

<!-- Updated by scripts/update-docs-from-abigen, do not edit this section manually. -->

None.

## RETURN VALUE

**nanosleep**() always returns **ZX_OK**.

## SEE ALSO

[deadline_after](deadline_after.md),
[xz_timer_create](timer_create.md),
[timer_set](timer_set.md),
[timer_cancel](timer_cancel.md),
