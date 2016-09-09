# mx_msgpipe_read

## NAME

msgpipe_read - read a message from a message pipe

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_msgpipe_read(mx_handle_t handle,
                            void* bytes, uint32_t* num_bytes,
                            mx_handle_t* handles, uint32_t* num_handles,
                            uint32_t flags);
```

## DESCRIPTION

**msgpipe_read**() attempts to read the first message from the message
pipe specified by *handle* into the provided *bytes* and/or *handles*
buffers.

The pointers *num_bytes* and *num_handles* are used to specify the
size of the read buffers, and upon return may be used to indicate the
actual size of the message read (upon success) or the size of the
message (upon failure due to the provided buffers being too small.
If these pointers are NULL, their respective buffer sizes are understood
to be zero.

Message pipe messages may contain both byte data and handle payloads
and may only be read in their entirety.  Partial reads are not possible.

## RETURN VALUE

**msgpipe_read**() returns **NO_ERROR** on success, and the uint32_t's
pointed at by *num_bytes* and/or *num_handles* (provided they are
non-NULL) are updated to reflect the exact size of the byte and handle
payloads of the message read.

## ERRORS

**ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ERR_WRONG_TYPE**  *handle* is not a message pipe handle.

**ERR_INVALID_ARGS**  *num_bytes* (if non-NULL) is an invalid pointer
or *num_handles* (if non-NULL) is an invalid pointer, or *bytes* is
non-NULL but *num_bytes* is NULL, or *handles* is non-NULL but
*num_handles* is null, or *handles* or *num_handles* (if non-NULL) are
invalid pointers.

**ERR_ACCESS_DENIED**  *handle* does not have **MX_RIGHT_READ**.

**ERR_BAD_STATE**  The message pipe contained no messages to read.

**ERR_REMOTE_CLOSED**  The other side of the message pipe is closed.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BUFFER_TOO_SMALL**  The provided *bytes* or *handles* buffers
are too small (in which case, the minimum sizes necessary to receive
the message will be written to the uint32_t's pointed at by these
parameters, provided they are non-NULL).

## NOTES

*num_handles* is a pointer to a count of the number of elements in
the *handles* array, not its size in bytes.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[handle_wait_one](handle_wait_one),
[handle_wait_many](handle_wait_many.md),
[msgpipe_create](msgpipe_create.md),
[msgpipe_write](msgpipe_write.md).
