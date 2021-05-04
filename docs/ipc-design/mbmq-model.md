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
`enqueued_as_reply`.

The caller process can then read the MBO from its MsgQueue using
`zx_msgqueue_read()`.  The caller supplies a CMH object but in this
case the CMH is not used.  The caller can use the key value returned
by `zx_msgqueue_read()` to determine which MBO was returned, if
necessary.  The syscall removes the MBO from the MsgQueue and sets the
MBO's state back to `owned_by_callee`.  The caller can now read the
reply message from the MBO using `zx_mbo_read()`.
