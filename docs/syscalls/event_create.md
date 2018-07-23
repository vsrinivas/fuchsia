# zx_event_create

## NAME

event_create - create an event

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_event_create(uint32_t options, zx_handle_t* out);
```

## DESCRIPTION

**event_create**() creates an event, which is an object that is signalable. That
is, its *ZX_USER_SIGNAL_n* (where *n* is 0 through 7) signals can be
manipulated using **object_signal**().

The newly-created handle will have the *ZX_RIGHT_TRANSFER*, *ZX_RIGHT_DUPLICATE*,
*ZX_RIGHT_READ*, *ZX_RIGHT_WRITE*, and *ZX_RIGHT_SIGNAL* rights.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**event_create**() returns ZX_OK and a valid event handle (via *out*) on success.
On failure, an error value is returned.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *out* is an invalid pointer, or *options* is nonzero.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[eventpair_create](eventpair_create.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[handle_replace](handle_replace.md),
[object_signal](object_signal.md).
