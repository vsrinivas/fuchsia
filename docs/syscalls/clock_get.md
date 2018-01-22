# zx_clock_get

## NAME

clock_get - Acquire the current time.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_time_t zx_clock_get(uint32_t clock_id)
```

## DESCRIPTION

**zx_clock_get**() returns the current time of *clock_id*, or 0 if *clock_id* is
invalid.

## SUPPORTED CLOCK IDS

*ZX_CLOCK_MONOTONIC* number of nanoseconds since the system was powered on.

*ZX_CLOCK_UTC* number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

*ZX_CLOCK_THREAD* number of nanoseconds the current thread has been running for.

## RETURN VALUE

On success, **zx_clock_get**() returns the current time according to the given clock ID.

## ERRORS

On error, **zx_clock_get**() currently returns 0.
