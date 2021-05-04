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
