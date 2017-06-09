# mx_timer_create

## NAME

timer_create - create a timer

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_timer_create(uint32_t options, uint32_t clock_id, mx_handle_t* out);

```

## DESCRIPTION

**timer_create**() creates a timer, an object that can signal
when a specified point in time has been reached. The only valid
value for *options* is zero and the only valid *clock_id* is
MX_CLOCK_MONOTONIC

The returned handle has the MX_RIGHT_DUPLICATE, MX_RIGHT_TRANSFER,
MX_RIGHT_READ and MX_RIGHT_WRITE right.

## RETURN VALUE

**timer_create**() returns **MX_OK** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**MX_ERR_INVALID_ARGS**  *out* is an invalid pointer or NULL or
*options* or *clock_id* is any value other than MX_CLOCK_MONOTONIC.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[timer_start](timer_start.md),
[timer_cancel](timer_cancel.md),
[deadline_after](deadline_after.md),
[handle_close](handle_close.md)
