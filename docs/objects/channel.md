# Channel

## NAME

channel - Bidirectional interprocess communication

## SYNOPSIS

A channel is a bidirectional transport of messages consisting of some
amount of byte data and some number of handles.

## DESCRIPTION

Channels maintain an ordered queue of messages to be delivered in
either direction. A message consists of some amount of data and some
number of handles. A call to *zx_channel_write()* enqueues one message,
and a call to *zx_channel_read()* dequeues one message (if any are
queued). A thread can block until messages are pending via
*zx_object_wait_one()* or other waiting mechanisms.

Alternatively, a call to *zx_channel_call()* enqueues a message in one
direction of the channel, waits for a corresponding response, and
dequeues the response message. In call mode, corresponding responses
are identified via the first 4 bytes of the message, called the
transaction ID. Coming up with distinct transaction IDs is up to the
users of *zx_channel_call()*.

The process of sending a message via a channel has two steps. The
first is to atomically write the data into the channel and move
ownership of all handles in the message into this channel. This
operation cannot partially succeed: at the end of the call, all
handles are either still in the calling process's handle table or are
all in the channel. The second operation is similar: after a channel
read, all the handles in the next message to read are either
atomically moved into the process's handle table, all remain in the
channel, or are discarded (only when the
**ZX_CHANNEL_READ_MAY_DISCARD** option is given).

Unlike many other kernel object types, channels are not
duplicatable. Thus there is only ever one handle associated to a
handle endpoint.

Because of these properties (that channel messages move their handle
contents atomically, and that channels are not duplicatable), the
kernel is able to avoid complicated garbage collection, lifetime
management, or cycle detection simply by enforcing the simple rule
that a channel handle may not be written into itself.

## SYSCALLS

+ [channel_call](../syscalls/channel_call.md) - synchronously send a message and receive a reply
+ [channel_create](../syscalls/channel_create.md) - create a new channel
+ [channel_read](../syscalls/channel_read.md) - receive a message from a channel
+ [channel_write](../syscalls/channel_write.md) - write a message to a channel

<br>

+ [object_wait_one](../syscalls/object_wait_one.md) - wait for signals on one object

## SEE ALSO

+ [Zircon concepts](../concepts.md)
+ [Handles](../handles.md)
