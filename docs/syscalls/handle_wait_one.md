# mx_handle_wait_one

## NAME

handle_wait_one - wait for signals on a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_wait_one(mx_handle_t handle,
                               mx_signals_t signals,
                               mx_time timeout,
                               mx_signals_t* pending);
```

## DESCRIPTION

**handle_wait_one**() is a blocking syscall which causes the caller to
wait until at least one of the specified *signals* is pending on *handle*
or *timeout* elapses.

Upon return, if non-NULL, *pending* is a bitmap of all of the
signals which are pending on *handle*.

It is possible to have the call return with *pending* different than the
state that caused the wait to complete if another thread is further modifying
the object behind *handle*.

The *timeout* parameter is relative time (from now) in nanoseconds which
takes two special values: **0** and **MX_TIME_INFINITE**. The former causes
the wait to complete immediately and the latter signals that wait will
never times out.

## RETURN VALUE

**handle_wait_one**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *pending* is an invalid pointer.

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** and may
not be waited upon.

**ERR_HANDLE_CLOSED**  *handle* was invalidated (e.g., closed) during the wait.

## BUGS

Currently the smallest *timeout* is 1 millisecond. Intervals smaller
than that are equivalent to 1ms.
