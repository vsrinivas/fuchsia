# zx_channel_read

## NAME

channel_read - read a message from a channel

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_channel_read(zx_handle_t handle, uint32_t options,
                            void* bytes, zx_handle_t* handles,
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

**channel_read**() returns **ZX_OK** on success, if *actual_bytes*
and *actual_handles* (if non-NULL), contain the exact number of bytes
and count of handles read.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**ZX_ERR_INVALID_ARGS**  If any of *bytes*, *handles*, *actual_bytes*, or
*actual_handles* are non-NULL and an invalid pointer.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_READ**.

**ZX_ERR_SHOULD_WAIT**  The channel contained no messages to read.

**ZX_ERR_PEER_CLOSED**  The other side of the channel is closed.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ZX_ERR_BUFFER_TOO_SMALL**  The provided *bytes* or *handles* buffers
are too small (in which case, the minimum sizes necessary to receive
the message will be written to *actual_bytes* and *actual_handles*,
provided they are non-NULL). If *options* has **ZX_CHANNEL_READ_MAY_DISCARD**
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
