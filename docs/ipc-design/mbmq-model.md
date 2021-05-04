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

MBOs have two roles.  An MBO stores a message, and it also acts as a
path by which a reply message is returned from a callee to a caller.

When a process (the caller) sends a request, this involves writing a
request message into an MBO and sending the MBO on a channel.  The
receiving process (the callee) receives a limited-access reference to
the MBO through a CMH object, from which it can read the request
message.  To return a reply, the callee writes the reply message into
the MBO and tells the MBO to return itself to the caller, which then
allows the caller to read the reply from the MBO.

## Overview of the request-reply lifecycle

There are three possible states for a MBO:

*   `caller_owns`: Owned by the caller: contents accessible through
    the MBO handle
*   `queue_owns`: Enqueued on a MsgQueue or channel
*   `callee_owns`: Owned by a callee via a CMH: contents accessible
    through the CMH handle

An MBO switches between these states as it is sent to a callee,
received, and sent back to the caller.

An MBO starts off in the `caller_owns` state.

To send a request, the caller process writes the request message into
the MBO using `zx_mbo_write()` and then sends the MBO on a channel
using `zx_channel_write_mbo()`.  This enqueues the MBO onto the
channel's associated MsgQueue and switches the MBO's state to
`queue_owns`.  In that state, MBO's handle can no longer be used to
read or write the MBO, so the caller cannot modify the message after
it has been sent.

The callee process can read the MBO from its MsgQueue using
`zx_msgqueue_read()`, supplying a CMH object.  This removes the MBO
from the message queue, sets the CMH to point to the MBO, and sets the
MBO's state to `callee_owns`.  This state gives the callee the ability
to read and write the MBO's contents using the CMH handle.  The caller
can read the request message out of the MBO by pasing the CMH handle
to `zx_mbo_read()`.  The caller can write a reply message into the MBO
(overwriting its contents) by passing the CMH handle to
`zx_mbo_write()`.

Once the callee has written a reply into the MBO, it can send the
reply to the caller by passing the CMH handle to
`zx_cmh_send_reply()`.  This enqueues the MBO on its associated
MsgQueue, drops the CMH's reference to the MBO (putting the CMH back
in the "unused" state), and sets the MBO's state to `queue_owns`
again.

The caller process can then read the MBO from its MsgQueue using
`zx_msgqueue_read()`.  The caller supplies a CMH object but in this
case the CMH is not used.  The caller can use the key value returned
by `zx_msgqueue_read()` to determine which MBO was returned, if
necessary.  The syscall removes the MBO from the MsgQueue and sets the
MBO's state back to `callee_owns`.  The caller can now read the reply
message from the MBO using `zx_mbo_read()`.
