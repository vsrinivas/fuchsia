# zx_object_wait_one

## NAME

object_wait_one - wait for signals on an object

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_wait_one(zx_handle_t handle,
                               zx_signals_t signals,
                               zx_time_t deadline,
                               zx_signals_t* observed);
```

## DESCRIPTION

**object_wait_one**() is a blocking syscall which causes the caller to
wait until at least one of the specified *signals* has been observed on
the object *handle* refers to or *deadline* passes.

Upon return, if non-NULL, *observed* is a bitmap of *all* of the
signals which were observed asserted on that object while waiting.

The *observed* signals may not reflect the actual state of the object's
signals if the state of the object was modified by another thread or
process.  (For example, a Channel ceases asserting **ZX_CHANNEL_READABLE**
once the last message in its queue is read).

The *deadline* parameter specifies a deadline with respect to
**ZX_CLOCK_MONOTONIC**.  **ZX_TIME_INFINITE** is a special value meaning wait forever.

## RETURN VALUE

**object_wait_one**() returns **ZX_OK** if any of *signals* were observed
on the object before *deadline* passes.

In the event of **ZX_ERR_TIMED_OUT**, *observed* may reflect state changes
that occurred after the deadline passed, but before the syscall returned.

For any other return value, *observed* is undefined.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *observed* is an invalid pointer.

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WAIT** and may
not be waited upon.

**ZX_ERR_CANCELED**  *handle* was invalidated (e.g., closed) during the wait.

**ZX_ERR_TIMED_OUT**  The specified deadline passed before any of the specified
*signals* are observed on *handle*.

**ZX_ERR_NOT_SUPPORTED**  *handle* is a handle that cannot be waited on
(for example, a Port handle).

## SEE ALSO

[object_wait_async](object_wait_async.md),
[object_wait_many](object_wait_many.md).
