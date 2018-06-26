# zx_channel_call

## NAME

channel_call - send a message to a channel and await a reply

## SYNOPSIS

```
#include <zircon/syscalls.h>

typedef struct {
    void* wr_bytes;
    zx_handle_t* wr_handles;
    void *rd_bytes;
    zx_handle_t* rd_handles;
    uint32_t wr_num_bytes;
    uint32_t wr_num_handles;
    uint32_t rd_num_bytes;
    uint32_t rd_num_handles;
} zx_channel_call_args_t;

zx_status_t zx_channel_call(zx_handle_t handle, uint32_t options,
                            zx_time_t deadline, zx_channel_call_args_t* args,
                            uint32_t* actual_bytes, uint32_t* actual_handles);
```

## DESCRIPTION

**channel_call**() is like a combined **channel_write**(), **object_wait_one**(),
and **channel_read**(), with the addition of a feature where a transaction id at
the front of the message payload *bytes* is used to match reply messages with send
messages, enabling multiple calling threads to share a channel without any additional
userspace bookkeeping.

The write and read phases of this operation behave like **channel_write**() and
**channel_read**() with the difference that their parameters are provided via the
*zx_channel_call_args_t* structure.

The first four bytes of the written and read back messages are treated as a
transaction ID of type **zx_txid_t**.  The kernel generates a txid for the
written message, replacing that part of the message as read from userspace.
The kernel generated txid will be between 0x80000000 and 0xFFFFFFFF, and will
not collide with any txid from any other **channel_call**() in progress against
this channel endpoint.  If the written message has a length of fewer than four
bytes, an error is reported.

When the outbound message is written, simultaneously an interest is registered
for inbound messages of the matching txid.

While *deadline* has not passed, if an inbound message arrives with a matching txid,
instead of being added to the tail of the general inbound message queue, it is delivered
directly to the thread waiting in **zx_channel_call**().

If such a reply arrives after *deadline* has passed, it will arrive in the general
inbound message queue, cause **ZX_CHANNEL_READABLE** to be signaled, etc.

Inbound messages that are too large to fit in *rd_num_bytes* and *rd_num_handles*
are discarded and **ZX_ERR_BUFFER_TOO_SMALL** is returned in that case.

As with **zx_channel_write**(), the handles in *handles* are always consumed by
**zx_channel_call**() and no longer exist in the calling process.

## RETURN VALUE

**channel_call**() returns **ZX_OK** on success and the number of bytes and
count of handles in the reply message are returned via *actual_bytes* and
*actual_handles*, respectively.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle, any element in
*handles* is not a valid handle, or there are duplicates among the handles
in the *handles* array.

**ZX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**ZX_ERR_INVALID_ARGS**  any of the provided pointers are invalid or null,
or *wr_num_bytes* is less than four, or *options* is nonzero.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WRITE** or
any element in *handles* does not have **ZX_RIGHT_TRANSFER**.

**ZX_ERR_PEER_CLOSED**  The other side of the channel was closed or became
closed while waiting for the reply.

**ZX_ERR_CANCELED**  *handle* was closed while waiting for a reply.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

**ZX_ERR_OUT_OF_RANGE**  *wr_num_bytes* or *wr_num_handles* are larger than the
largest allowable size for channel messages.

**ZX_ERR_BUFFER_TOO_SMALL**  *rd_num_bytes* or *rd_num_handles* are too small
to contain the reply message.

**ZX_ERR_NOT_SUPPORTED**  one of the handles in *handles* was *handle*
(the handle to the channel being written to).

## NOTES

The facilities provied by **channel_call**() can interoperate with message dispatchers
using **channel_read**() and **channel_write**() directly, provided the following rules
are observed:

1. A server receiving synchronous messages via **channel_read**() should ensure that the
txid of incoming messages is reflected back in outgoing responses via **channel_write**()
so that clients using **channel_call**() can correctly route the replies.

2. A client sending messages via **channel_write**() that will be replied to should ensure
that it uses txids between 0 and 0x7FFFFFFF only, to avoid colliding with other threads
communicating via **channel_call**().

If a **channel_call**() returns due to **ZX_ERR_TIMED_OUT**, if the server eventually replies,
at some point in the future, the reply *could* match another outbound request (provided about
2^31 **channel_call**()s have happened since the original request.  This syscall is designed
around the expectation that timeouts are generally fatal and clients do not expect to continue
communications on a channel that is timing out.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[channel_create](channel_create.md),
[channel_read](channel_read.md),
[channel_write](channel_write.md).
