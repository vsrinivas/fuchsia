# zx_clock_get

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

clock_get - Acquire the current time.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_time_t zx_clock_get(zx_clock_t clock_id);
```

## DESCRIPTION

`zx_clock_get()` returns the current time of *clock_id*, or 0 if *clock_id* is
invalid.

## SUPPORTED CLOCK IDS

**ZX_CLOCK_MONOTONIC** number of nanoseconds since the system was powered on.

**ZX_CLOCK_UTC** number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

**ZX_CLOCK_THREAD** number of nanoseconds the current thread has been running for.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

On success, `zx_clock_get()` returns the current time according to the given clock ID.

## ERRORS

On error, `zx_clock_get()` returns 0.
