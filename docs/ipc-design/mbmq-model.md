# Message Buffer Message Queue (MBMQ) model for IPC

This is an outline of a set of primitives for IPC (interprocess
communication) proposed for use in Fuchsia.

## Overview of object types

The MBMQ model is based around four types of object:

*   Channel endpoint
*   MsgQueue: message queue
*   MBO: message buffer object
*   CMH: callee MBO holder

A channel endpoint is something that you can send messages to.  A
channel may be redirected to a MsgQueue so that when a message is sent
to the channel, the message is enqueued onto the MsgQueue.

A MsgQueue is a queue of messages, potentially from multiple sources
-- a MsgQueue may be set as the destination for multiple channels.

MBOs have multiple roles:

*   An MBO stores a message.  A message consists of an array of bytes
    (data) and an array of Zircon handles (object-capabilities).  This
    message can be a request or a reply.

*   MBOs are passed back and forth between caller and callee
    processes, which will read or write the MBO.  The caller writes a
    request message into the MBO, which the callee reads.  The callee
    replaces the request with a reply message, which the caller reads.
    Ownership of the MBO is transferred so that, at any given point in
    time, only the caller or the callee can read or write the MBO's
    contents.

*   An MBO acts as a reply path for a request, by which the reply is
    returned from a callee to a caller.  Each MBO may have an
    associated reply-path MsgQueue, which is the queue that the MBO
    will be enqueued on when a callee returns it via
    `zx_cmh_send_reply()`.  This means a callee does not have to
    specify a channel for returning a reply message on.

A CMH is a callee process's limited-access reference to an MBO.  A
callee process can use a CMH to read and write an MBO until it returns
ownership of the MBO back to the caller.  The callee's access to the
MBO is revoked when it sends the reply, so the caller can then reuse
the MBO with a different callee.

## Overview of the request-reply lifecycle

There are four possible states for an MBO, listed here in the order in
which they are typically used:

*   `owned_by_caller`: Owned by the caller: contents accessible through
    the MBO handle
*   `enqueued_as_request`: Enqueued on a MsgQueue (or channel) as a
    request
*   `owned_by_callee`: Owned by a callee via a CMH: contents accessible
    through the CMH handle
*   `enqueued_as_reply`: Enqueued on a MsgQueue as a reply

An MBO switches between these states as it is sent to a callee,
received, and sent back to the caller.

An MBO starts off in the `owned_by_caller` state.

To send a request, the caller process writes the request message into
the MBO using `zx_mbo_write()` and then sends the MBO on a channel
using `zx_channel_write_mbo()`.  This enqueues the MBO onto the
channel's associated MsgQueue and switches the MBO's state to
`enqueued_as_request`.  In that state, MBO's handle can no longer be
used to read or write the MBO, so the caller cannot modify the message
after it has been sent.

The callee process can read the MBO from its MsgQueue using
`zx_msgqueue_read()`, supplying a CMH object.  This removes the MBO
from the message queue, sets the CMH to point to the MBO, and sets the
MBO's state to `owned_by_callee`.  This state gives the callee the
ability to read and write the MBO's contents using the CMH handle.
The caller can read the request message out of the MBO by pasing the
CMH handle to `zx_mbo_read()`.  The caller can write a reply message
into the MBO (overwriting its contents) by passing the CMH handle to
`zx_mbo_write()`.

Once the callee has written a reply into the MBO, it can send the
reply to the caller by passing the CMH handle to
`zx_cmh_send_reply()`.  This enqueues the MBO on its associated
MsgQueue, drops the CMH's reference to the MBO (putting the CMH back
in the "unused" state), and sets the MBO's state to
`enqueued_as_reply`.  The callee can now reuse this CMH object in
later calls to `zx_msgqueue_read()`.

The caller process can then read the MBO from its MsgQueue using
`zx_msgqueue_read()`.  The caller supplies a CMH object but in this
case the CMH is not used.  The `zx_msgqueue_read()` syscall removes
the MBO from the MsgQueue and sets the MBO's state back to
`owned_by_callee`.  The caller can use the key value returned by
`zx_msgqueue_read()` to determine which MBO was returned, if
necessary.  The caller can now read the reply message from the MBO
using `zx_mbo_read()`.

The cycle can now repeat.  The caller can now write a new request
message into the MBO and send it on a channel as above, potentially to
a different callee.

## Operations

*   `zx_mbo_create() -> mbo`: Creates a new MBO.  The MBO starts off
    in the `owned_by_caller` state.

*   `zx_mbo_read(mbo_or_cmh) -> (data, handles)`: Reads an MBO.  This
    reads the entire contents of the MBO.  (Operations for partial
    reads will be defined later.)  This takes either an MBO handle (in
    the `owned_by_caller` state) or a CMH handle (for a CMH pointing
    to an MBO in the `owned_by_callee` state).

*   `zx_mbo_write(mbo_or_cmh, data, handles)`: Writes an MBO.  This
    replaces the MBO's existing contents.  (Operations for
    partial/incremental writes will be defined later.)  This takes
    either an MBO handle (in the `owned_by_caller` state) or a CMH
    handle (for a CMH pointing to an MBO in the `owned_by_callee`
    state).

*   `zx_channel_write_mbo(channel, mbo)`: Sends an MBO as a request on
    a channel.  The MBO must be in the `owned_by_caller` state.  Its
    state will be changed to `enqueued_as_request`.

    If the channel has an associated destination MsgQueue, as set by
    `zx_channel_redirect()`, the MBO will be enqueued onto that
    MsgQueue and its `key` field will be set to the key value that was
    set by `zx_channel_redirect()`.  Otherwise, the MBO will be
    enqueued on the channel so that it is readable from the channel's
    opposite endpoint.

*   `zx_msgqueue_create() -> msgqueue`: Creates a new MsgQueue.

*   `zx_msgqueue_read(msgqueue, cmh) -> key`: Reads an MBO from a
    MsgQueue.  If the MsgQueue is empty, this first blocks until the
    MsgQueue is non-empty.

    This takes a CMH as an argument.  The CMH must not currently hold
    a reference to an MBO, otherwise an error is returned.

    This removes the first MBO from the message queue.  If the MBO was
    in the `enqueued_as_request` state, this sets the CMH to point to
    the MBO, and changes the MBO's state to `owned_by_callee`.

    If the MBO was in the `enqueued_as_reply` state, this changes the
    MBO's state to `owned_by_caller` and does not modify the CMH.

    This returns the `key` field from the MBO, which allows the
    process to determine which channel the message was sent on (for
    requests) or which request this is a reply to (for replies).

*   `zx_channel_redirect(channel_or_mbo, msgqueue, key)`: Sets the
    associated destination MsgQueue for a channel or an MBO.

    For channels: When given channel endpoint 1 of a pair, this sets
    the associated MsgQueue for endpoint 2 of the pair so that calls
    to `zx_channel_write_mbo()` on endpoint 2 will enqueue messages
    onto the given MsgQueue with the given key value.  If the channel
    had any existing messages queued on it (previously written to
    endpoint 2 and currently readable from endpoint 1), they are moved
    onto the given MsgQueue.

    For MBOs: When given an MBO, this sets the associated MsgQueue for
    the MBO, onto which `zx_cmh_send_reply()` will enqueue the MBO
    with the given key value.  The MBO must be in the
    `owned_by_caller` state.

*   `zx_cmh_create() -> cmh`: Creates a new CMH.  The CMH starts off
    not holding a reference to any MBO.

*   `zx_cmh_send_reply(cmh)`: Returns a reply message to the caller.
    The given CMH must have a reference to an MBO (which will be in
    the state `owned_by_callee`).  This operation drops the CMH's
    reference to the MBO, and enqueues the MBO onto its associated
    MsgQueue, setting the MBO's `key` field to its reply key.  The
    MBO's state is set to `enqueued_as_reply`.

*   `zx_object_wait_async_mbo(handle, mbo, signals, options)`: This is
    a replacement for `zx_object_wait_async()`.  Like that syscall, it
    waits until one or more of the given signals is asserted on the
    object specified by `handle`.  The difference is that rather than
    returning the notification by sending a port packet to a port, the
    new syscall returns the notification as a reply on the given MBO.
    The notification is returned as if by an invocation of
    `zx_cmd_send_reply()`, enqueuing the MBO onto its associated reply
    queue.

    This means that waiting for a signal on an object is like making a
    call to the object.

    The given MBO must be in the `owned_by_caller` state.

    Note that `zx_object_wait_async_mbo()` does not need to allocate
    memory.  We can ensure that every MBO preallocates enough memory
    for the bookkeeping for waiting for a signal.  In contrast,
    `zx_object_wait_async()` must allocate memory each time it is
    called.

    Note that while `zx_object_wait_async()` is commonly used for
    waiting for messages on channels in Fuchsia today, this is not
    necessary in the MBMQ model where messages (MBOs) are enqueued
    directly onto MsgQueues.

    We might need to define an equivalent to `zx_port_cancel()`.

Handle-closing: operations when all references or handles to an object
are dropped:

*   MBO: An MBO is freed when all references to it have been dropped.

    Closing all the handles to an MBO only causes the MBO to be freed
    if there are no other references to the MBO from a MsgQueue or a
    CMH.  If the handles to an MBO are closed while the MBO is
    enqueued as a request on a MsgQueue, the MBO remains in the
    MsgQueue, and it can still be read into a CMH and sent as a reply,
    but it will be freed when `zx_msgqueue_read()` returns the MBO to
    the `owned_by_caller` state.

    This means it is possible to do a "send and forget" with an MBO:
    that is, send the MBO as a request message, but close the MBO
    handle and ignore any replies.

*   Automatic replies: An MBO receives an automatic reply message if
    it was sent as a request but there is no way a callee could send a
    reply.  There are two cases for this:

    *   Closed CMH: If a CMH is closed while it holds a reference to
        an MBO, the system will send an automatic reply on the MBO.
        The system will replace the MBO's contents with a default
        reply message and send the MBO as a reply (as if
        `zx_cmh_send_reply()` was called).

    *   Closed MsgQueue: If all the handles to a MsgQueue are closed
        while its queue contains MBOs in the state
        `enqueued_as_request`, or if MBOs are enqueued onto the
        MsgQueue after all handles to the MsgQueue were closed, then
        the system will send automatic replies on those MBOs.  This
        does not apply to MBOs in the state `enqueued_as_reply`
        because these are already replies.

    This means that if a callee process crashes in the middle of
    processing a request from a caller, or before unqueuing the
    request, the caller will not be left waiting for a reply message
    indefinitely.

## State for each object type

This section gives a summary of the state that is stored by each of
the object types.

MBO:

*   Message contents.  This consists of two resizable arrays:
    *   An array of bytes (data).
    *   An array of Zircon handles.
*   `key`: 64-bit integer.  This is set when the MBO is enqueued onto
    a MsgQueue by either `zx_channel_write_mbo()` or
    `zx_cmh_send_reply()`.  Its value is returned by
    `zx_msgqueue_read()`.
*   `reply_queue`: This is the MsgQueue that `zx_cmh_send_reply()`
    will enqueue the MBO onto when it is sent as a reply.
*   `reply_key`: 64-bit integer.  `zx_cmh_send_reply()` will set the
    MBO's `key` field to this value when the MBO is sent as a reply.
*   State: one of the four MBO states listed above (`owned_by_caller`,
    `owned_by_callee`, `enqueued_as_request`, `enqueued_as_reply`).
    Note that in practice we do not need to distinguish between
    `enqueued_as_request` and `owned_by_callee`.  Operations on MBO
    handles need to check for `owned_by_caller`, whereas
    `zx_msgqueue_read()` needs to check for `enqueued_as_request`
    versus `enqueued_as_reply`.

CMH:

*   Reference to an MBO.  This reference may be null.  If the
    reference is non-null, the MBO is in the `owned_by_callee` state.

MsgQueue:

*   List of MBOs, all of which will be in the state
    `enqueued_as_request` or `enqueued_as_reply`.

Channel endpoint:

*   Reference to a MsgQueue.  This reference may be null.
*   `channel_key`: 64-bit integer.  When an MBO is sent through this
    channel endpoint, its `key` field will be set to this
    `channel_key` value.
*   List of MBOs, all of which will be in the state
    `enqueued_as_request`.  This will be empty if the endpoint has an
    associated MsgQueue.

## "Fire-and-forget" requests: requests without replies

At the FIDL level, some request messages are "fire-and-forget": they
have no corresponding reply message.

In the MBMQ model, each request generally has an associated reply
message, but it may be an empty or automatic reply, and the caller may
choose to ignore it.

For fire-and-forget requests, a caller has a choice of whether it
recycles MBOs across requests or not:

*   Non-recycled MBOs: This is the simplest for a caller to do, so it
    is likely to be the common case.  The caller allocates a new MBO
    for each fire-and-forget message.  The caller closes the MBO
    handle after sending the MBO, without ever setting a `reply_queue`
    on the MBO.  The MBO will get freed automatically after the callee
    has received the message and dropped its reference to the MBO.

*   Recycled MBOs: A callee has the option of detecting when the
    callee has dropped its reference to the MBO.  It can exercise that
    option by setting a `reply_queue` on the MBO in order to receive a
    reply, just as with MBOs where non-trivial replies are expected.
    This may be useful for a resource-conscious caller, which can use
    this ability to recycle MBOs between requests or to implement flow
    control.

## A note on terminology: "caller" and "callee"

We are using the terms "caller" and "callee" to emphasise that these
roles are relative to a particular interaction.  The "caller" is the
process that sends a request and may later receive a reply.  The
"callee" is the process that receives a request and may send a reply.
We can't use the terms "sender" and "receiver" for these two roles
because the caller and callee may both send and receive messages.

An alternative pair of terms would be "client" and "server".  We are
avoiding those terms, for two reasons:

*   Firstly, a process that is a server in one interaction can be the
    client in another interaction.
*   Secondly, a server process may send callback messages to its
    clients (e.g. send-only request messages).  In such cases, we
    choose to say that the server remains a server but acts as a
    caller when sending a message to its clients.

The terms "caller" and "callee" are commonly used in the context of
programming languages and compilers where it is clear that a function
may be a callee in one case and a caller in another case.

## Acknowledgements

The concept of MBOs, with the MBO acting as both a reusable message
buffer and a return path, is due to Corey Tabaka.
