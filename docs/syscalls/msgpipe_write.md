# mx_msgpipe_write

## NAME

msgpipe_write - write a message to a message pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_msgpipe_write(mx_handle_t handle,
                             void* bytes, uint32_t num_bytes,
                             mx_handle_t* handles, uint32_t num_handles,
                             uint32_t flags);
```

## DESCRIPTION

**msgpipe_write**() attempts to write a message of *num_bytes*
bytes and *num_handles* handles to the message pipe specified by
*handle*.  The pointers *handles* and *bytes* may be null if their
respective sizes are zero.

On success, all *num_handles* of the handles in the *handles* array
are no longer accessible to the caller's process -- they are attached
to the message and will become available to the reader of that message
from the opposite end of the message pipe.  On any failure, all handles
remain accessible to the caller's process and are not transferred.

If the *handle* message pipe is a reply pipe, *handle* must be
included as the last handle in the *handles* array.

If the *handle* message pipe is not a reply pipe, it is invalid
to include *handle* in the *handles* array.

To create a reply pipe, use MX_FLAG_REPLY_PIPE in the
**msgpipe_create**() call.

## RETURN VALUE

**msgpipe_write**() returns **NO_ERROR** on success.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle or any of *handles*
are not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a message pipe handle.

**ERR_INVALID_ARGS**  *bytes* is an invalid pointer, or *handles*
is an invalid pointer, or if there are duplicates among the handles
in the *handles* array.

**ERR_NOT_SUPPORTED**  *handle* was found in the *handles* array
and the pipe is not a reply pipe.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE** or
any of *handles* do not have **MX_RIGHT_TRANSFER**.

**ERR_BAD_STATE**  The other side of the message pipe is closed or
the pipe is a reply pipe and the reply pipe handle was not included
as the last element of the *handles* array.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_TOO_BIG**  *num_bytes* or *num_handles* are larger than the
largest allowable size for message pipe messages.

**ERR_NOT_SUPPORTED**  one of the handles in *handles* was *handle*
(the handle to the message pipe being written to).

## NOTES

*num_handles* is a count of the number of elements in the *handles*
array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[msgpipe_create](msgpipe_create.md),
[msgpipe_read](msgpipe_read.md).
