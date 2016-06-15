# _magenta_handle_wait_one

## NAME

handle_wait_one - wait for signals on a handle

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t _magenta_handle_wait_one(mx_handle_t handle, mx_signals_t signals,
                                     mx_signals_t* satisfied_signals,
                                     mx_time timeout,
                                     mx_signals_t* satisfiable_signals);
```

## DESCRIPTION

**handle_wait_one**() is a blocking syscall which causes the caller to
wait until at least one of the specified *signals* is pending on *handle*
or *timeout* elapses.

Upon return, if non-NULL, *satisfied_signals* is a bitmap of all of the
signals which are pending on *handle* and *satisfiable_signals* is
a bitmap of all of the signals which are possible to be pending on
*handle*, given its type and current state.

It is possible to have the call return with a *satisfiable_signals*
value being different than the value that caused the wait to complete
if another thread is further modifing the object behind *handle*.

The *timeout* parameter is relative time (from now) in nanoseconds which
takes two special values: **0** and **MX_TIME_INFINITE**. The former causes
the wait to complete immediately and the latter signals that wait will
never times out.

## RETURN VALUE

**handle_wait_one**() returns **NO_ERROR** on success or **ERR_TIMED_OUT**
if the wait completed because *timeout* nanoseconds have elapsed.

In the event of **ERR_TIMED_OUT**, *satisfied_signals* may indicate
signals that were satisfied after the timeout but before the syscall
returned.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle or *satisfied_signals*
or *satisfiable_signals* were invalid pointers.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ** and may
not be waited upon.

## BUGS

Currently the *satisfiable_signals* return value is filled with the same
content as the *satisifed_signals* value.
