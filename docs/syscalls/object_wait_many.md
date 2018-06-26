# zx_object_wait_many

## NAME

object_wait_many - wait for signals on multiple objects

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_wait_many(zx_wait_item_t* items, size_t count, zx_time_t deadline);

typedef struct {
    zx_handle_t handle;
    zx_signals_t waitfor;
    zx_signals_t pending;
} zx_wait_item_t;
```

## DESCRIPTION

**object_wait_many**() is a blocking syscall which causes the caller to
wait until either the *deadline* passes or at least one of the specified
signals is asserted by the object to which the associated handle refers.
If an object is already asserting at least one of the specified signals,
the wait ends immediately.

The caller must provide *count* zx_wait_item_ts in the *items* array,
containing the handle and signals bitmask to wait for for each item.

The *deadline* parameter specifies a deadline with respect to
**ZX_CLOCK_MONOTONIC**.  **ZX_TIME_INFINITE** is a special value meaning wait
forever.

Upon return, the *pending* field of *items* is filled with bitmaps indicating
which signals are pending for each item.

The *pending* signals in *items* may not reflect the actual state of the object's
signals if the state of the object was modified by another thread or
process.  (For example, a Channel ceases asserting **ZX_CHANNEL_READABLE**
once the last message in its queue is read).

The maximum number of items that may be waited upon is **ZX_WAIT_MANY_MAX_ITEMS**,
which is 8.  To wait on more things at once use [Ports](../objects/port.md).

## RETURN VALUE

**object_wait_many**() returns **ZX_OK** if any of *waitfor* signals were
observed on their respective object before *deadline* passed.

In the event of **ZX_ERR_TIMED_OUT**, *items* may reflect state changes
that occurred after the deadline pased, but before the syscall returned.

In the event of **ZX_ERR_CANCELED**, one or more of the items being waited
upon have had their handles closed, and the *pending* field for those items
will have the **ZX_SIGNAL_HANDLE_CLOSED** bit set.

For any other return value, the *pending* fields of *items* are undefined.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *items* isn't a valid pointer.

**ZX_ERR_OUT_OF_RANGE**  *count* is greater than **ZX_WAIT_MANY_MAX_ITEMS**.

**ZX_ERR_BAD_HANDLE**  one of *items* contains an invalid handle.

**ZX_ERR_ACCESS_DENIED**  One or more of the provided *handles* does not
have **ZX_RIGHT_WAIT** and may not be waited upon.

**ZX_ERR_CANCELED**  One or more of the provided *handles* was invalidated
(e.g., closed) during the wait.

**ZX_ERR_TIMED_OUT**  The specified deadline passed before any of the specified signals are
observed on any of the specified handles.

**ZX_ERR_NOT_SUPPORTED**  One of the *items* contains a handle that cannot
be waited one (for example, a Port handle).

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## BUGS

*pending* more properly should be called *observed*.

## SEE ALSO

[object_wait_many](object_wait_many.md),
[object_wait_one](object_wait_one.md).
