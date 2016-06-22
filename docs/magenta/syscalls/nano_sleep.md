# _magenta_nanosleep

## NAME

nanosleep - high resolution sleep

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t _magenta_nanosleep(mx_time_t nanoseconds);
```

## DESCRIPTION

**nanosleep**() suspends the calling thread execution for
at least *nanoseconds* nanoseconds. The special value **MX_TIME_INFINITE**
suspends the calling thread execution indefinitely. The value **0** immediately
yields the thread.

## RETURN VALUE

**nanosleep**() returns **NO_ERROR** on success.

## ERRORS

## BUGS

Currently the smallest nonzero sleep is 1 millisecond. Intervals smaller
than that are equivalent to 1ms.
