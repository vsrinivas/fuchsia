# zx_clock_get

## NAME

clock_get - Acquire the current time.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_clock_get_new(uint32_t clock_id, zx_time_t* out_time);
zx_time_t zx_clock_get(zx_clock_t clock_id);
```

## DESCRIPTION

**zx_clock_get**() returns the current time of *clock_id*, or 0 if *clock_id* is
invalid.

**zx_clock_get_new** returns the current time of *clock_id* via
  *out_time*, and returns whether *clock_id* was valid.

## SUPPORTED CLOCK IDS

*ZX_CLOCK_MONOTONIC* number of nanoseconds since the system was powered on.

*ZX_CLOCK_UTC* number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

*ZX_CLOCK_THREAD* number of nanoseconds the current thread has been running for.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

On success, **zx_clock_get**() returns the current time according to the given clock ID.

On success, **zx_clock_get_new**() returns *ZX_OK*.

## ERRORS

On error, **zx_clock_get**() currently returns 0.

**ZX_ERR_INVALID_ARGS**  *clock_id* is not a valid clock id, or *out_time* is an invalid pointer.
