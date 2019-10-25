# Channel

## NAME

channel - Bidirectional interprocess communication

## SYNOPSIS

A channel is a bidirectional transport of messages consisting of some
amount of byte data and some number of handles.

## DESCRIPTION

Channels maintain an ordered queue of messages to be delivered in either
direction. A message consists of some amount of data and some number of handles.
A call to [`zx_channel_write()`] enqueues one message, and a call to
[`zx_channel_read()`] dequeues one message (if any are queued). A thread can block
until messages are pending via [`zx_object_wait_one()`] or other waiting
mechanisms.

Alternatively, a call to [`zx_channel_call()`] enqueues a message in one
direction of the channel, waits for a corresponding response, and
dequeues the response message. In call mode, corresponding responses
are identified via the first 4 bytes of the message, called the
transaction ID. The kernel supplies distinct transaction IDs (always with the
high bit set) for messages written with [`zx_channel_call()`].

The process of sending a message via a channel has two steps. The first is to
atomically write the data into the channel and move ownership of all handles in
the message into this channel. This operation always consumes the handles: at
the end of the call, all handles either are all in the channel or are all
discarded. The second operation, channel read, is similar: on success
all the handles in the next message are atomically moved into the
receiving process' handle table. On failure, the channel retains
ownership unless the **ZX_CHANNEL_READ_MAY_DISCARD** option
is specified, then they are dropped.

Unlike many other kernel object types, channels are not duplicatable. Thus, there
is only ever one handle associated with a channel endpoint, and the process holding
that handle is considered the owner. Only the owner can read or write messages or send
the channel endpoint to another process.

Furthermore, when ownership of a channel endpoint goes from one process to
another, even if a write was in progress, the ordering of messages is guaranteed
to be parsimonious; messages before the transfer event originate from the
previous owner and messages after the transfer belong to the new owner. The same
applies if a read was in progress when the endpoint was transferred.

The above sequential guarantee is not provided for other kernel objects, even if
the last remaining handle is stripped of the **ZX_RIGHT_DUPLICATE** right.

## SYSCALLS

 - [`zx_channel_call()`] - synchronously send a message and receive a reply
 - [`zx_channel_create()`] - create a new channel
 - [`zx_channel_read()`] - receive a message from a channel
 - [`zx_channel_write()`] - write a message to a channel

<br>

 - [`zx_object_wait_one()`] - wait for signals on one object

## SEE ALSO

+ [Zircon concepts](/docs/concepts/kernel/concepts.md)
+ [Handles](/docs/concepts/objects/handles.md)

[`zx_channel_call()`]: /docs/reference/syscalls/channel_call.md
[`zx_channel_create()`]: /docs/reference/syscalls/channel_create.md
[`zx_channel_read()`]: /docs/reference/syscalls/channel_read.md
[`zx_channel_write()`]: /docs/reference/syscalls/channel_write.md
[`zx_object_wait_one()`]: /docs/reference/syscalls/object_wait_one.md
