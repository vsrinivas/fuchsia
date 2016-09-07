# mx_event_create

## NAME

event_create - create an event

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_handle_t mx_event_create(uint32_t options);
```

## DESCRIPTION

**event_create**() creates an event, which is an object that is signalable. That
is, its *MX_SIGNAL_SIGNALn* (where *n* is 0 through 4) signals can be
manipulated using **object_signal**().

The newly-created handle will have the *MX_RIGHT_TRANSER*, *MX_RIGHT_DUPLICATE*,
*MX_RIGHT_READ*, and *MX_RIGHT_WRITE* rights.

Note: *options* is currently ignored.

## RETURN VALUE

**event_create**() returns a valid event handle (strictly positive) on success.
On failure, a (strictly) negative error value is returned. Zero (the "invalid
handle") is never returned.

## ERRORS

**ERR_NO_MEMORY**  Temporary failure due to lack of memory.

## SEE ALSO

[eventpair_create](eventpair_create.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[handle_replace](handle_replace.md),
[object_signal](object_signal.md).
