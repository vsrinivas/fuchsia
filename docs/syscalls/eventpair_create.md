# zx_eventpair_create

## NAME

eventpair_create - create an event pair

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_eventpair_create(uint32_t options, zx_handle_t* out0, zx_handle_t* out1);
```


## DESCRIPTION

**eventpair_create**() creates an event pair, which is a pair of objects that
are mutually signalable.

The signals *ZX_EVENTPAIR_SIGNALED* and *ZX_USER_SIGNAL_n* (where *n* is 0 through 7)
may be set or cleared using **object_signal**() (modifying the signals on the
object itself), or **object_signal_peer**() (modifying the signals on its
counterpart).

When all the handles to one of the objects have been closed, the
*ZX_EVENTPAIR_PEER_CLOSED* signal will be asserted on the opposing object.

The newly-created handles will have the *ZX_RIGHT_TRANSFER*,
*ZX_RIGHT_DUPLICATE*, *ZX_RIGHT_READ*, *ZX_RIGHT_WRITE*, *ZX_RIGHT_SIGNAL*,
and *ZX_RIGHT_SIGNAL_PEER* rights.

Currently, no options are supported, so *options* must be set to 0.


## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**eventpair_create**() returns **ZX_OK** on success. On failure, a (negative)
error code is returned.


## ERRORS

**ZX_ERR_INVALID_ARGS**  *out0* or *out1* is an invalid pointer or NULL.

**ZX_ERR_NOT_SUPPORTED**  *options* has an unsupported flag set (i.e., is not 0).

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.


## SEE ALSO

[event_create](event_create.md),
[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[handle_replace](handle_replace.md),
[object_signal](object_signal.md),
[object_signal_peer](object_signal.md).
