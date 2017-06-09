# mx_object_wait_many

## NAME

object_wait_many - wait for signals on multiple objects

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_object_wait_many(mx_wait_item_t* items, uint32_t count, mx_time_t deadline);

typedef struct {
    mx_handle_t handle;
    mx_signals_t waitfor;
    mx_signals_t pending;
} mx_wait_item_t;
```

## DESCRIPTION

**object_wait_many**() is a blocking syscall which causes the caller to
wait until at least one of the specified signals is pending on one of
the specified *items* or *deadline* passes.

The caller must provide *count* mx_wait_item_ts in the *items* array,
containing the handle and signals bitmask to wait for for each item.

The *deadline* parameter specifies a deadline with respect to
**MX_CLOCK_MONOTONIC**.  **MX_TIME_INFINITE** is a special value meaning wait forever.

Upon return, the *pending* field of *items* is filled with bitmaps indicating
which signals are pending for each item.

The *pending* signals in *items* may not reflect the actual state of the object's
signals if the state of the object was modified by another thread or
process.  (For example, a Channel ceases asserting **MX_CHANNEL_READABLE**
once the last message in its queue is read).

## RETURN VALUE

**object_wait_many**() returns **MX_OK** if any of *waitfor* signals were
observed on their respective object before *deadline* passed.

In the event of **MX_ERR_TIMED_OUT**, *items* may reflect state changes
that occurred after the deadline pased, but before the syscall returned.

For any other return value, the *pending* fields of *items* are undefined.

## ERRORS

**MX_ERR_INVALID_ARGS**  *items* isn't a valid pointer or if *count* is too large.

**MX_ERR_BAD_HANDLE**  one of *items* contains an invalid handle.

**MX_ERR_ACCESS_DENIED**  One or more of the provided *handles* does not
have **MX_RIGHT_READ** and may not be waited upon.

**MX_ERR_CANCELED**  One or more of the provided *handles* was invalidated
(e.g., closed) during the wait.

**MX_ERR_TIMED_OUT**  The specified deadline passed before any of the specified signals are
observed on any of the specified handles.

**MX_ERR_NOT_SUPPORTED**  One of the *items* contains a handle that cannot
be waited one (for example, a Port handle).

**MX_ERR_NO_MEMORY** (Temporary) failure due to lack of memory.

## BUGS

*pending* more properly should be called *observed*.

## SEE ALSO

[object_wait_many](object_wait_many.md),
[object_wait_one](object_wait_one.md).
