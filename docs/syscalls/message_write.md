# _magenta_message_write

## NAME

message_write - write a message to a message pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t _magenta_message_write(mx_handle_t handle,
                                   void* bytes, uint32_t num_bytes,
                                   mx_handle_t* handles, uint32_t num_handles,
                                   uint32_t flags);
```

## DESCRIPTION

**message_write**() attempts to write a message of *num_bytes* 
bytes and *num_handles* handles to the message pipe specified by
*handle*.  The pointers *handles* and *bytes* may be null if their
respective sizes are zero.

## RETURN VALUE

**message_write**() returns **NO_ERROR** on success.

## ERRORS

**ERR_INVALID_ARGS**  *handle* isn't a valid message pipe handle, or
*bytes* is an invalid pointer, or *handles* is an invalid pointer,
or any of the handles passed via the *handles* array are invalid
handles.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_WRITE**.

**ERR_BAD_STATE**  The other side of the message pipe is closed.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_TOO_BIG**  *num_bytes* or *num_handles* are larger than the
largest allowable size for message pipe messages.

## NOTES

*num_handles* is a count of the number of elements in the *handles*
array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md).
[handle_duplicate](handle_duplicate.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[message_pipe_create](message_pipe_create.md),
[message_read](message_read.md).

