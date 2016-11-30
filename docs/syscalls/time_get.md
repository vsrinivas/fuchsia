# mx_time_get

## NAME

time_get - Acquire the current time.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_time_t mx_time_get(uint32_t clock_id)
```

## DESCRIPTION

**mx_current_time**() returns the current time of *clock_id*, or 0 if *clock_id* is
invalid.

## SUPPORTED CLOCK IDS

*MX_CLOCK_MONOTONIC* number of nanoseconds since the system was powered on.

*MX_CLOCK_UTC* number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

## RETURN VALUE

**mx_time_get**() returns zero on error.

## ERRORS

## BUGS
