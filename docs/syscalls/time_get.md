# mx_time_get

## NAME

time_get - Acquire the current time.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_time_t mx_time_get(uint32_t clock_id)
```

## DESCRIPTION

**mx_time_get**() returns the current time of *clock_id*, or 0 if *clock_id* is
invalid.

## SUPPORTED CLOCK IDS

*MX_CLOCK_MONOTONIC* number of nanoseconds since the system was powered on.

*MX_CLOCK_UTC* number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

*MX_CLOCK_THREAD* number of nanoseconds the current thread has been running for.

## RETURN VALUE

On success, **mx_time_get**() returns the current time according to the given clock ID.

## ERRORS

On error, **mx_time_get**() currently returns 0.
