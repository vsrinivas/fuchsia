# zx_clock_get

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Acquire the current time.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_clock_get(zx_clock_t clock_id, zx_time_t* out);
```

## DESCRIPTION

`zx_clock_get()` returns the current time of *clock_id* via
*out*, and returns whether *clock_id* was valid.

## SUPPORTED CLOCK IDS

**ZX_CLOCK_MONOTONIC** number of nanoseconds since the system was powered on.

**ZX_CLOCK_UTC** number of wall clock nanoseconds since the Unix epoch (midnight on January 1 1970) in UTC

**ZX_CLOCK_THREAD** number of nanoseconds the current thread has been running for.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

TODO(fxbug.dev/32253)

## RETURN VALUE

On success, `zx_clock_get()` returns **ZX_OK**.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *clock_id* is not a valid clock id, or *out* is an invalid pointer.
