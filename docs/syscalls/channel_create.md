# mx_channel_create

## NAME

channel_create - create a channel

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_channel_create(uint32_t flags,
                              mx_handle_t* out0, mx_handle_t* out1);

```

## DESCRIPTION

**channel_create**() creates a channel, a bi-directional
datagram-style message transport capable of sending raw data bytes
as well as handles from one side to the other.

Two handles are returned on success, providing access to both sides
of the channel.  Messages written to one handle may be read from
the opposite.

The handles will have *MX_RIGHT_TRANSFER* (allowing them to be sent
to another process via channel write), *MX_RIGHT_WRITE* (allowing
messages to be written to them), and *MX_RIGHT_READ* (allowing messages
to be read from them).

The *flags* can be either 0 or *MX_FLAG_REPLY_PIPE*. A reply pipe
behaves like a regular channel except for **mx_channel_write**()
which must include itself as the last handle being transfered.

When *flags* is *MX_FLAG_REPLY_PIPE*, only *handles[1]* is a reply
pipe. *handles[0]* is a regular pipe.


## RETURN VALUE

**channel_create**() returns **NO_ERROR** on success. In the event
of failure, a negative error value is returned.

## ERRORS

**ERR_INVALID_ARGS**  *handles* is an invalid pointer or NULL or
*flags* is any value other than 0 or *MX_CHANNEL_CREATE_REPLY_PIPE*.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[channel_read](channel_read.md),
[channel_write](channel_write.md).
