# mx_event_create

## NAME

event_create - create an event

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_event_create(uint32_t options, mx_handle_t* out);
```

## DESCRIPTION

**event_create**() creates an event, which is an object that is signalable. That
is, its *MX_USER_SIGNAL_n* (where *n* is 0 through 7) signals can be
manipulated using **object_signal**().

The newly-created handle will have the *MX_RIGHT_TRANSFER*, *MX_RIGHT_DUPLICATE*,
*MX_RIGHT_READ*, *MX_RIGHT_WRITE*, and *MX_RIGHT_SIGNAL* rights.

## RETURN VALUE

**event_create**() returns MX_OK and a valid event handle (via *out*) on success.
On failure, an error value is returned.

## ERRORS

**MX_ERR_INVALID_ARGS**  *out* is an invalid pointer, or *options* is nonzero.

**MX_ERR_NO_MEMORY**  Temporary failure due to lack of memory.

## SEE ALSO

[eventpair_create](eventpair_create.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[handle_replace](handle_replace.md),
[object_signal](object_signal.md).
