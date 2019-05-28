# zx_channel_write_etc

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

Write a message to a channel.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_channel_write_etc(zx_handle_t handle,
                                 uint32_t options,
                                 const void* bytes,
                                 uint32_t num_bytes,
                                 zx_handle_disposition_t* handles,
                                 uint32_t num_handles);
```

## DESCRIPTION

This syscall has not been implemented yet.

Like [`zx_channel_write()`] it attempts to write a message of *num_bytes*
bytes and *num_handles* handles to the channel specified by *handle*, but in
addition it will perform operations in the handles that are being
transfered with *handles* being an array of `zx_handle_disposition_t`:

```
typedef struct zx_handle_disposition {
    zx_handle_op_t operation;
    zx_handle_t handle;
    zx_rights_t rights;
    zx_obj_type_t type;
    zx_status_t result;
} zx_handle_disposition_t;
```

With *handle* the source handle, *rights* the desired final rights and
*result* must have exactly the value **ZX_OK**. All source handles must have
at least **ZX_RIGHT_TRANSFER** right.

The *type* is used to perform validation of the object type that the caller
expects *handle* to be. It can be *ZX_OBJ_TYPE_NONE* to perform no validation
check or one of `zx_obj_type_t` defined types. If any validation check fails
the entire operation fails.

The operation applied to *handle* is one of:

*   **ZX_HANDLE_OP_MOVE**: The *handle* will be transfered with rights *rights*
which can be *ZX_RIGHT_SAME_RIGHTS* or a reduced set of rights. This is
equivalent to issuing [`zx_handle_replace()`] except that this operation can
also remove the *ZX_RIGHT_TRANSFER* on the transfered handle.

*   **ZX_HANDLE_OP_DUPLICATE**: The *handle* will be duplicated with rights
rights* which can be *ZX_RIGHT_SAME_RIGHTS* or a reduced set of rights. This
is equivalent to issuing [`zx_handle_duplicate()`] except that this operation
can also remove the *ZX_RIGHT_TRANSFER* on the duplicated handle.

If an operation fails, the error code for that source handle is written to
*result*. All operations in the *handles* array are attempted, even if one or
more operations fail.

All operations in all handles must succeed for the channel write to
succeed.

On success all source handles with **ZX_HANDLE_OP_MOVE** are no longer
accessible to the caller's process -- they are attached to the message and will
become available to the reader of that message from the opposite end of the
channel. On any failure, these handles are discarded rather than transferred.

On success or failure, all source handles with **ZX_HANDLE_OP_DUPLICATE**
will still be accessible to the caller's process.

It is invalid to include *handle* (the handle of the channel being written
to) in the *handles* array (the handles being sent in the message).

The maximum number of handles which may be sent in a message is
**ZX_CHANNEL_MAX_MSG_HANDLES**, which is 64.

The maximum number of bytes which may be sent in a message is
**ZX_CHANNEL_MAX_MSG_BYTES**, which is 65536.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_CHANNEL** and have **ZX_RIGHT_WRITE**.

Every entry of *handles* must have **ZX_RIGHT_TRANSFER**.

## RETURN VALUE

`zx_channel_write_etc()` returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle, any source handle in
*handles* is not a valid handle, or there are duplicates among the handles
in the *handles* array.

**ZX_ERR_WRONG_TYPE**  *handle* is not a channel handle, any source handle
in *handles* did not match the object type *type*.

**ZX_ERR_INVALID_ARGS**  *bytes* is an invalid pointer, *handles*
is an invalid pointer, or *options* is nonzero.

**ZX_ERR_NOT_SUPPORTED**  *handle* was found in the *handles* array, or
one of the handles in *handles* was *handle* (the handle to the
channel being written to).

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WRITE** or
any source handle in *handles* does not have **ZX_RIGHT_TRANSFER**.

**ZX_ERR_PEER_CLOSED**  The other side of the channel is closed.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_OUT_OF_RANGE**  *num_bytes* or *num_handles* are larger than the
largest allowable size for channel messages.

## NOTES

If the caller removes the the **ZX_RIGHT_TRANSFER** to a handle attached
to a message, the reader of the message will receive a handle that cannot
be written to any other channel, but still can be using according to its
rights and can be closed if not needed.

## SEE ALSO

 - [`zx_channel_call()`]
 - [`zx_channel_create()`]
 - [`zx_channel_read()`]
 - [`zx_channel_read_etc()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_channel_call()`]: channel_call.md
[`zx_channel_create()`]: channel_create.md
[`zx_channel_read()`]: channel_read.md
[`zx_channel_read_etc()`]: channel_read_etc.md
[`zx_channel_write()`]: channel_write.md
[`zx_handle_duplicate()`]: handle_duplicate.md
[`zx_handle_replace()`]: handle_replace.md
