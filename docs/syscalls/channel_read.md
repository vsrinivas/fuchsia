# mx_channel_read

## NAME

channel_read - read a message from a channel

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_channel_read(mx_handle_t handle, uint32_t options,
                            void* bytes, mx_handle_t* handles,
                            uint32_t num_bytes, uint32_t num_handles,
                            uint32_t* actual_bytes, uint32_t* actual_handles);
```

## DESCRIPTION

**channel_read**() attempts to read the first message from the channel
specified by *handle* into the provided *bytes* and/or *handles* buffers.

The parameters *num_bytes* and *num_handles* are used to specify the
size of the read buffers.

Channel messages may contain both byte data and handle payloads and may
only be read in their entirety.  Partial reads are not possible.

The *bytes* buffer is written before the *handles* buffer. In the event of
overlap between these two buffers, the contents written to *handles*
will overwrite the portion of *bytes* it overlaps.

## RETURN VALUE

**channel_read**() returns **MX_OK** on success, if *actual_bytes*
and *actual_handles* (if non-NULL), contain the exact number of bytes
and count of handles read.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**MX_ERR_INVALID_ARGS**  If any of *bytes*, *handles*, *actual_bytes*, or
*actual_handles* are non-NULL and an invalid pointer.

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ**.

**MX_ERR_SHOULD_WAIT**  The channel contained no messages to read.

**MX_ERR_PEER_CLOSED**  The other side of the channel is closed.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**MX_ERR_BUFFER_TOO_SMALL**  The provided *bytes* or *handles* buffers
are too small (in which case, the minimum sizes necessary to receive
the message will be written to *actual_bytes* and *actual_handles*,
provided they are non-NULL). If *options* has **MX_CHANNEL_READ_MAY_DISCARD**
set, then the message is discarded.

## NOTES

*num_handles* and *actual_handles* are counts of the number of elements
in the *handles* array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[channel_call](channel_call.md),
[channel_create](channel_create.md),
[channel_write](channel_write.md).
