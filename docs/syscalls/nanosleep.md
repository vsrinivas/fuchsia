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
**MX_CLOCK_MONOTONIC**. The special value **MX_TIME_INFINITE** suspends the calling
thread execution indefinitely. The value **0** immediately yields the thread.

## RETURN VALUE

**nanosleep**() returns **NO_ERROR** on success.

## ERRORS
