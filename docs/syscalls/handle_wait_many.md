# mx_handle_wait_many

## NAME

handle_wait_many - wait for signals on multiple handles

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_wait_many(uint32_t count, const mx_handle_t* handles,
                                const mx_signals_t* signals,
                                mx_time_t timeout,
                                uint32_t* result_index,
                                mx_signals_state_t* signals_states);
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

Upon return, if non-null, the *signals_states* array is filled with a pair of
bitmaps for each of the *count* specified handles, indicating which signals are
pending and which are satisfiable on that particular handle.

It is possible to have the call return with a *signals_states* array with values
different than the values that caused the wait to complete if other threads are
further modifing the objects behind the *handles*.

If non-null and the return value is **NO_ERROR** or **ERR_HANDLE_CLOSED**,
*result_index* set to first handle that satisfied the wait (or was closed,
respectively).

## RETURN VALUE

**handle_wait_many**() returns **NO_ERROR** on success when the wait was
satisfied by the *signals* input, **ERR_BAD_STATE** when one of the *signals*
inputs became unsatisfiable, or **ERR_TIMED_OUT** if the wait completed because
*timeout* nanoseconds have elapsed.

In the event of **ERR_TIMED_OUT**, *signals_states* may reflect state changes
that occurred after the timeout but before the syscall returned.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid handle or *result_index* or
or *signals_states* were invalid pointers.

**ERR_ACCESS_DENIED**  One or more of the provided *handles* does not
have **MX_RIGHT_READ** and may not be waited upon.

**ERR_HANDLE_CLOSED**  One or more of the provided *handles* was invalidated
(e.g., closed) during the wait.

**ERR_NO_MEMORY** (Temporary) failure due to lack of memory.

## BUGS

Currently the smallest *timeout* is 1 millisecond. Intervals smaller
than that are equivalent to 1ms.
