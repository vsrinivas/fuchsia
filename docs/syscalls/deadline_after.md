# mx_deadline_after

## NAME

deadline_after - Convert a time relative to now to an absolute deadline

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_time_t mx_deadline_after(mx_duration_t nanoseconds)
```

## DESCRIPTION

**mx_deadline_after**() is a utility for converting from now-relative durations
to absolute deadlines.

## RETURN VALUE

**mx_deadline_after**() returns the absolute time (with respect to **CLOCK_MONOTONIC**)
that is *nanoseconds* nanoseconds from now.

## ERRORS

**mx_deadline_after**() does not report any error conditions.

## EXAMPLES

```
// Sleep 50 milliseconds
mx_time_t deadline = mx_deadline_after(MX_MSEC(50));
mx_nanosleep(deadline);
```

## SEE ALSO

[ticks_get](ticks_get.md)
