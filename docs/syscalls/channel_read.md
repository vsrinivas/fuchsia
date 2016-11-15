# mx_channel_read

## NAME

channel_read - read a message from a channel

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_channel_read(mx_handle_t handle, uint32_t flags,
                            void* bytes,
                            uint32_t num_bytes, uint32_t* actual_bytes
                            mx_handle_t* handles,
                            uint32_t num_handles, uint32_t* actual_handles);
```

## DESCRIPTION

**channel_read**() attempts to read the first message from the channel
specified by *handle* into the provided *bytes* and/or *handles* buffers.

The parameters *num_bytes* and *num_handles* are used to specify the
size of the read buffers.

 and upon return may be used to indicate the
actual size of the message read (upon success) or the size of the
message (upon failure due to the provided buffers being too small.
If these pointers are NULL, their respective buffer sizes are understood
to be zero.

Channel messages may contain both byte data and handle payloads and may
only be read in their entirety.  Partial reads are not possible.

## RETURN VALUE

**channel_read**() returns **NO_ERROR** on success, if *actual_bytes*
and *actual_handles* (if non-NULL), contain the exact number of bytes
and count of handles read.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a channel handle.

**ERR_INVALID_ARGS**  If any of *bytes*, *handles*, *actual_bytes*, or
*actual_handles* are non-NULL and an invalid pointer.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ**.

**ERR_SHOULD_WAIT**  The channel contained no messages to read.

**ERR_REMOTE_CLOSED**  The other side of the channel is closed.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BUFFER_TOO_SMALL**  The provided *bytes* or *handles* buffers
are too small (in which case, the minimum sizes necessary to receive
the message will be written to *actual_bytes* and *actual_handles*,
provided they are non-NULL). If *flags* has **MX_CHANNEL_READ_MAY_DISCARD**
set, then the message is discarded.

## NOTES

*num_handles* and *actual_handles* are counts of the number of elements
in the *handles* array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[handle_wait_one](handle_wait_one.md),
[handle_wait_many](handle_wait_many.md),
[channel_create](channel_create.md),
[channel_write](channel_write.md).
