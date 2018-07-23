# zx_nanosleep

## NAME

nanosleep - high resolution sleep

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_nanosleep(zx_time_t deadline);
```

## DESCRIPTION

**nanosleep**() suspends the calling thread execution until *deadline* passes on
**ZX_CLOCK_MONOTONIC**. The value **0** immediately yields the thread.

To sleep for a duration, use [**zx_deadline_after**](deadline_after.md) and the
**ZX_\<time-unit\>** helpers:

```
#include <zircon/syscalls.h> // zx_deadline_after, zx_nanosleep
#include <zircon/types.h> // ZX_MSEC et al.

// Sleep 50 milliseconds
zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
```

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**nanosleep**() always returns **ZX_OK**.
