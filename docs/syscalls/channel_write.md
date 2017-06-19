# mx_channel_write

## NAME

channel_write - write a message to a channel

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_channel_write(mx_handle_t handle, uint32_t options,
                             void* bytes, uint32_t num_bytes,
                             mx_handle_t* handles, uint32_t num_handles);
```

## DESCRIPTION

**channel_write**() attempts to write a message of *num_bytes*
bytes and *num_handles* handles to the channel specified by
*handle*.  The pointers *handles* and *bytes* may be NULL if their
respective sizes are zero.

On success, all *num_handles* of the handles in the *handles* array
are no longer accessible to the caller's process -- they are attached
to the message and will become available to the reader of that message
from the opposite end of the channel.  On any failure, all handles
remain accessible to the caller's process and are not transferred.

It is invalid to include *handle* (the handle of the channel being written
to) in the *handles* array (the handles being sent in the message).

The maximum number of handles which may be sent in a message is
*MX_CHANNEL_MAX_MSG_HANDLES*, which is 64.

The maximum number of bytes which may be sent in a message is
*MX_CHANNEL_MAX_MSG_BYTES*, which is 65536.


## RETURN VALUE

**channel_write**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle or any element in
*handles* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**MX_ERR_INVALID_ARGS**  *bytes* is an invalid pointer, or *handles*
is an invalid pointer, or if there are duplicates among the handles
in the *handles* array, or *options* is nonzero.

**MX_ERR_NOT_SUPPORTED** *handle* was found in the *handles* array, or
one of the handles in *handles* was *handle* (the handle to the
channel being written to).

**MX_ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE** or
any element in *handles* does not have **MX_RIGHT_TRANSFER**.

**MX_ERR_PEER_CLOSED**  The other side of the channel is closed.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**MX_ERR_OUT_OF_RANGE**  *num_bytes* or *num_handles* are larger than the
largest allowable size for channel messages.

## NOTES

*num_handles* is a count of the number of elements in the *handles*
array, not its size in bytes.

The byte size limitation on messages is not yet finalized.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[channel_call](channel_call.md),
[channel_create](channel_create.md),
[channel_read](channel_read.md).
