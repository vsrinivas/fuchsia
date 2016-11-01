# mx_handle_wait_many

## NAME

handle_wait_many - wait for signals on multiple handles

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_handle_wait_many(mx_wait_item_t* items, uint32_t count, mx_time_t timeout);

typedef struct {
    mx_handle_t handle;
    mx_signals_t waitfor;
    mx_signals_t pending;
} mx_wait_item_t;
```

## DESCRIPTION

**handle_wait_many**() is a blocking syscall which causes the caller to
wait until at least one of the specified signals is pending on one of
the specified *items* or *timeout* elapses.

The caller must provide *count* mx_wait_item_ts in the *items* array,
containing the handle and signals bitmask to wait for for each item.

The *timeout* parameter is relative time (from now) in nanoseconds which
takes two special values: **0** and **MX_TIME_INFINITE**. The former causes
the wait to complete immediately and the latter signals that wait will
never times out.

Upon return, the *pending* field of *items* is filled with bitmaps indicating
which signals are pending for each item.

It is possible to have the call return with a *items* array with values
different than the values that caused the wait to complete if other threads are
further modifing the objects behind the *handles*.

## RETURN VALUE

**handle_wait_many**() returns **NO_ERROR** on success.

In the event of **ERR_TIMED_OUT**, *items* may reflect state changes
that occurred after the timeout but before the syscall returned.

## ERRORS

**ERR_INVALID_ARGS**  *items* isn't a valid pointer.

**ERR_BAD_HANDLE**  one of *items* contains an invalid handle.

**ERR_ACCESS_DENIED**  One or more of the provided *handles* does not
have **MX_RIGHT_READ** and may not be waited upon.

**ERR_HANDLE_CLOSED**  One or more of the provided *handles* was invalidated
(e.g., closed) during the wait.

**ERR_NO_MEMORY** (Temporary) failure due to lack of memory.

## BUGS

Currently the smallest *timeout* is 1 millisecond. Intervals smaller
than that are equivalent to 1ms.
