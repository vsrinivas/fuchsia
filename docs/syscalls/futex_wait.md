# zx_futex_wait

## NAME

futex_wait - Wait on a futex.

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_futex_wait(const zx_futex_t* value_ptr, int32_t current_value,
                          zx_time_t deadline);
```

## DESCRIPTION

**futex_wait**() atomically verifies that *value_ptr* still contains the value
*current_value* and sleeps until the futex is made available by a call to
`zx_futex_wake`. Optionally, the thread can also be woken up after the
*deadline* (with respect to **ZX_CLOCK_MONOTONIC**) passes.

## SPURIOUS WAKEUPS

A component that uses futexes should be prepared to handle spurious
wakeups.  A spurious wakeup is a situation where **futex_wait**()
returns successfully even though the component did not wake the waiter
by calling **futex_wake**().

Zircon's implementation of futexes currently does not generate
spurious wakeups itself.  However, commonly-used algorithms that use
futexes can sometimes generate spurious wakeups.  For example, the
usual implementation of `mutex_unlock` can potentially produce a
**futex_wake**() call on a memory location after the location has been
freed and reused for unrelated purposes.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**futex_wait**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *value_ptr* is not a valid userspace pointer, or
*value_ptr* is not aligned.

**ZX_ERR_BAD_STATE**  *current_value* does not match the value at *value_ptr*.

**ZX_ERR_TIMED_OUT**  The thread was not woken before *deadline* passed.

## SEE ALSO

[futex_requeue](futex_requeue.md),
[futex_wake](futex_wake.md).
