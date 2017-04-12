# mx_futex_wait

## NAME

futex_wait - Wait on a futex.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_futex_wait(mx_futex_t* value_ptr, int current_value,
                          mx_time_t deadline);
```

## DESCRIPTION

Waiting on a futex (or acquiring it) causes a thread to sleep until
the futex is made available by a call to `mx_futex_wake`. Optionally,
the thread can also be woken up after the *deadline* (with respect
to **MX_CLOCK_MONOTONIC**) passes.

## RETURN VALUE

**futex_wait**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *value_ptr* is not a valid userspace pointer, or
*value_ptr* is not aligned.

**ERR_BAD_STATE**  *current_value* does not match the value at *value_ptr*.

**ERR_TIMED_OUT**  The thread was not woken before *deadline* passed.

## SEE ALSO

[futex_requeue](futex_requeue.md),
[futex_wake](futex_wake.md).
