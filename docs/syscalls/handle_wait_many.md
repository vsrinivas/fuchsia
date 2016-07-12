# mx_handle_wait_many

## NAME

handle_wait_many - wait for signals on multiple handles

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_wait_many(uint32_t count, const mx_handle_t* handles,
                                const mx_signals_t* signals,
                                mx_time_t timeout,
                                mx_signals_t* satisfied_signals,
                                mx_signals_t* satisfiable_signals);
```

## DESCRIPTION

**handle_wait_many**() is a blocking syscall which causes the caller to
wait until at least one of the specified *signals* is pending on one of
the specified handle *handles* or *timeout* elapses.

The caller must provide *count* handles in the *handles* array and *count*
signal bitmasks in the *signals* array. For each entry in the *handles*
array, the corresponding entry in *signals* indicates which signals the
calling thread should be resumed for.

The *timeout* parameter is relative time (from now) in nanoseconds which
takes two special values: **0** and **MX_TIME_INFINITE**. The former causes
the wait to complete immediately and the latter signals that wait will
never times out.

Upon return, if non-NULL, the *satisfied_signals* array is filled with
a bitmap for each of the *count* specified handles, indicating which
signals are pending on that particular handle.

It is possible to have the call return with a *satisfiable_signals* array
with values different than the values that caused the wait to complete
if other threads are further modifing the objects behind the *handles*.

If non-NULL, the *satisfiable_signals* is filled with a bitmap for each of
the specified handles indicating which signals are possible to be pending
on that handle, given its type and current state.

## RETURN VALUE

**handle_wait_many**() returns **NO_ERROR** on success when the wait was
satisfied by the *signals* input or **ERR_TIMED_OUT** if the wait completed
because *timeout* nanoseconds have elapsed.

In the event of **ERR_TIMED_OUT**, *satisfied_signals* may indicate
signals that were satisfied after the timeout but before the syscall
returned.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle or *satisfied_signals*
or *satisfiable_signals* were invalid pointers.

**ERR_ACCESS_DENIED**  One or more of the provided *handles* does not
have **MX_RIGHT_READ** and may not be waited upon.

**ERR_NO_MEMORY** (Temporary) failure due to lack of memory.

## BUGS

Currently the *satisfiable_signals* array is filled with the same content
as the *satisifed_signals* array.

Currently the smallest *timeout* is 1 millisecond. Intervals smaller
than that are equivalent to 1ms.
