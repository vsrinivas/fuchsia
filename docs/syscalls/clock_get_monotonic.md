# zx_clock_get_monotonic

## NAME

clock_get_monotonic - Acquire the current monotonic time.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_time_t zx_clock_get_monotonic(void);
```

## DESCRIPTION

**zx_clock_get_monotonic**() returns the current time in the system
monotonic clock. This is the number of nanoseconds since the system was
powered on.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_clock_get**() returns the current monotonic time.

## ERRORS

**zx_clock_get_monotonic**() cannot fail.
