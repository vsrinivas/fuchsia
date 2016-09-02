# mx_eventpair_create

## NAME

eventpair_create - create an event pair

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_eventpair_create(mx_handle_t handles[2], uint32_t flags);
```

## DESCRIPTION

**eventpair_create**() creates an event pair, which is a pair of objects that
are mutually signalable. That is, calling **object_signal**() on one object
(handle) actually sets the signals on the other object, and vice versa.
Destroying one object (by closing all the handles to it) sets the
*MX_SIGNAL_PEER_CLOSED* signal on the other object (if it is still alive); any
signals that were set remain set, while those that were not become
unsatisfiable.

The newly-created handles will have the *MX_RIGHT_TRANSER*,
*MX_RIGHT_DUPLICATE*, *MX_RIGHT_READ*, and *MX_RIGHT_WRITE* rights. The
*MX_SIGNAL_SIGNALn* (where *n* is 0 through 4) signals may be set using
**object_signal**() with the behavior described above.

Currently, no flags are supported, so *flags* must be set to 0.

## RETURN VALUE

**eventpair_create**() returns **NO_ERROR** on success. On failure, a (negative)
error code is returned.

## ERRORS

**ERR_INVALID_ARGS**  *handles* is an invalid pointer or NULL.

**ERR_NOT_SUPPORTED**  *flags* has an unsupported flag set (i.e., is not 0).

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[event_create](event_create.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[handle_replace](handle_replace.md),
[object_signal](object_signal.md).
