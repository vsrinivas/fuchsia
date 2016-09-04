# mx_msgpipe_create

## NAME

msgpipe_create - create a message pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_msgpipe_create(mx_handle_t handles[2], uint32_t flags);

```

## DESCRIPTION

**msgpipe_create**() creates a message pipe, a bi-directional
datagram-style message transport capable of sending raw data bytes
as well as handles from one side to the other.

Two handles are returned on success, providing access to both sides
of the message pipe.  Messages written to one handle may be read
from the opposite.

The handles will have *MX_RIGHT_TRANSFER* (allowing them to be sent
to another process via message pipe write), *MX_RIGHT_WRITE* (allowing
messages to be written to them), and *MX_RIGHT_READ* (allowing messages
to be read from them).

The *flags* can be either 0 or *MX_FLAG_REPLY_PIPE*. A reply pipe
behaves like a regular message pipe except for **mx_msgpipe_write**()
which must include itself as the last handle being transfered.

When *flags* is *MX_FLAG_REPLY_PIPE*, only *handles[1]* is a reply
pipe. *handles[0]* is a regular pipe.


## RETURN VALUE

**msgpipe_create**() returns **NO_ERROR** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_INVALID_ARGS**  *handles* is an invalid pointer or NULL or
*flags* is any value other than 0 or *MX_FLAG_REPLY_PIPE*.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[msgpipe_read](msgpipe_read.md),
[msgpipe_write](msgpipe_write.md).
