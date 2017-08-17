# mx_nanosleep

## NAME

nanosleep - high resolution sleep

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_nanosleep(mx_time_t deadline);
```

## DESCRIPTION

**nanosleep**() suspends the calling thread execution until *deadline* passes on
**MX_CLOCK_MONOTONIC**. The value **0** immediately yields the thread.

To sleep for a duration, use [**mx_deadline_after**](deadline_after.md) and the
**MX_\<time-unit\>** helpers:

```
#include <magenta/syscalls.h> // mx_deadline_after, mx_nanosleep
#include <magenta/types.h> // MX_MSEC et al.

// Sleep 50 milliseconds
mx_nanosleep(mx_deadline_after(MX_MSEC(50)));
```

## RETURN VALUE

**nanosleep**() always returns **MX_OK**.
