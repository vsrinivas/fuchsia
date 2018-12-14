# zx_timer_create

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

timer_create - create a timer

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_timer_create(uint32_t options,
                            zx_clock_t clock_id,
                            zx_handle_t* out);
```

## DESCRIPTION

`zx_timer_create()` creates a timer, an object that can signal
when a specified point in time has been reached. The only valid
*clock_id* is **ZX_CLOCK_MONOTONIC**.

The *options* value specifies the coalescing behavior which
controls whether the system can fire the time earlier or later
depending on other pending timers.

The possible values are:

+ **ZX_TIMER_SLACK_CENTER** coalescing is allowed with earlier and
  later timers.
+ **ZX_TIMER_SLACK_EARLY** coalescing is allowed only with earlier
  timers.
+ **ZX_TIMER_SLACK_LATE** coalescing is allowed only with later
  timers.

Passing 0 in options is equivalent to **ZX_TIMER_SLACK_CENTER**.

The returned handle has the **ZX_RIGHT_DUPLICATE**, **ZX_RIGHT_TRANSFER**,
**ZX_RIGHT_READ** and **ZX_RIGHT_WRITE** right.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

TODO(ZX-2399)

## RETURN VALUE

`zx_timer_create()` returns **ZX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL or
*options* is not one of the **ZX_TIMER_SLACK** values or *clock_id* is
any value other than **ZX_CLOCK_MONOTONIC**.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

 - [`zx_deadline_after()`]
 - [`zx_handle_close()`]
 - [`zx_timer_cancel()`]
 - [`zx_timer_set()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_deadline_after()`]: deadline_after.md
[`zx_handle_close()`]: handle_close.md
[`zx_timer_cancel()`]: timer_cancel.md
[`zx_timer_set()`]: timer_set.md
