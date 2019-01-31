# zx_clock_get_new

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

clock_get_new - Acquire the current time.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_clock_get_new(zx_clock_t clock_id, zx_time_t* out);
```

## DESCRIPTION

`zx_clock_get_new()` returns the current time of *clock_id* via
*out*, and returns whether *clock_id* was valid.

## SUPPORTED CLOCK IDS

**ZX_CLOCK_MONOTONIC** number of nanoseconds since the system was powered on.

**ZX_CLOCK_UTC** number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

**ZX_CLOCK_THREAD** number of nanoseconds the current thread has been running for.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

On success, `zx_clock_get_new()` returns **ZX_OK**.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *clock_id* is not a valid clock id, or *out* is an invalid pointer.
