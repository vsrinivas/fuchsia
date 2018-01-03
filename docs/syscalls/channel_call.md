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
                            uint32_t* actual_bytes, uint32_t* actual_handles,
                            zx_status_t* read_status);
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

The first four bytes (i.e., a leading **zx_txid_t**) of
*wr_bytes* are considered to be the transaction id (txid).  If there
are fewer than four bytes, the txid is considered to be zero.

When the outbound message is written, simultaneously an interest is registered
for inbound messages of the matching txid.

While *deadline* has not passed, if an inbound message arrives with a matching txid,
instead of being added to the tail of the general inbound message queue, it is delivered
directly to the thread waiting in **zx_channel_call**().

If such a reply arrives after *deadline* has passed, it will arrive in the general
inbound message queue, cause **ZX_CHANNEL_READABLE** to be signaled, etc.

Inbound messages that are too large to fit in *rd_num_bytes* and *rd_num_handles*
are discarded and **ZX_ERR_BUFFER_TOO_SMALL** is returned in that case.


## RETURN VALUE

**channel_call**() returns **ZX_OK** on success and the number of bytes and
count of handles in the reply message are returned via *actual_bytes* and
*actual_handles*, respectively.

The special return value **ZX_ERR_CALL_FAILED** indicates that the message was
sent, but an error occurred while waiting for a response.  This is necessary
to disambiguate errors like **ZX_ERR_PEER_CLOSED** which could have occurred
while attempting the write (in which case the caller would still own any handles
passed via *handles*) or while waiting (in which case the caller would no longer
own any of the handles).  The return parameter *read_status* is used to indicate
the specific error that occurred during the wait or read phase when **ZX_ERR_CALL_FAILED**
is returned.

In the event of **ZX_OK**, **ZX_ERR_TIMED_OUT**, or **ZX_ERR_CALL_FAILED**, the
handles in *handles* have been sent in a message to the other endpoint of the
Channel and no longer exist in the calling process.  In the event of any other
return values, the handles in *handles* remain in the calling process, unchanged.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle or any element in
*handles* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a channel handle.

**ZX_ERR_INVALID_ARGS**  any of the provided pointers are invalid or null,
or there are duplicates among the handles in the *handles* array,
or *options* is nonzero.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have **ZX_RIGHT_WRITE** or
any element in *handles* does not have **ZX_RIGHT_TRANSFER**.

**ZX_ERR_PEER_CLOSED**  The other side of the channel was closed or became
closed while waiting for the reply.

**ZX_ERR_CANCELED**  *handle* was closed while waiting for a reply.

**ZX_ERR_CALL_FAILED**  The write phase of the call succeeded, but an error occurred
while or after waiting for the response.  The specific error is returned via
*read_status* if it is non-null.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ZX_ERR_OUT_OF_RANGE**  *wr_num_bytes* or *wr_num_handles* are larger than the
largest allowable size for channel messages.

**ZX_ERR_BUFFER_TOO_SMALL**  *rd_num_bytes* or *rd_num_handles* are too small
to contain the reply message.

**ZX_ERR_NOT_SUPPORTED**  one of the handles in *handles* was *handle*
(the handle to the channel being written to).

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[handle_replace](handle_replace.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[channel_create](channel_create.md),
[channel_read](channel_read.md),
[channel_write](channel_write.md).
