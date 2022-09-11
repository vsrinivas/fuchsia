# MBMQ: Transition plan

See the [contents page](index.md) for other sections in this document.

This section outlines a transition plan for implementing the MBMQ
model in Fuchsia.

[TOC]

## Transition steps

Switching to using the MBMQ model can be done incrementally.  There
are some dependencies between the following steps, but some of them
could be reordered or done in parallel:

*   Implement most of the new kernel primitives, including MsgQueues,
    MBOs, CallersRefs and CalleesRefs.  Channels will be modified so
    that they can contain both legacy messages (those sent with
    `zx_channel_write()`) and MBO messages (those sent with
    `zx_channel_send()`).  MsgQueues will be added as a new object
    type, separate from Zircon ports.

*   Switch processes over to using MsgQueues instead of Zircon ports.
    An initial conversion can replace uses of `zx_object_wait_async()`
    with `zx_object_wait_async_mbo()`, but a later version should use
    `zx_object_set_msgqueue()` for waiting on channels, which should
    give some performance improvements.

*   Change all server processes to accept requests via both MBO
    messages and legacy messages.  Servers will send replies via
    either legacy messages or MBOs (using `zx_mbo_send_reply()`)
    depending on the request type.  This work can be made easier using
    the "legacy reply mode" for CalleesRefs described below.

*   Switch client code over to sending requests via MBOs instead of
    via legacy messages.  Change `zx_channel_call()` to send requests
    via MBOs.

*   The ability of servers to accept requests via legacy messages can
    now be dropped.

*   Convert users of FIDL event messages to use a separate channel for
    sending events.

*   Channels can now be made unidirectional.

*   Channels can be made shareable.  That is, we can allow channel
    handles to be duplicated.  We might want to hold off on this, or
    allow it selectively, if there are concerns about FIDL protocols
    that assume they have only one client process.

*   Legacy channel messages can be removed.

*   Zircon ports can be removed.

*   The transaction ID (`txid`) field can be removed from FIDL
    messages, because we no longer need it for matching up replies
    with requests.

## Legacy reply mode for CalleesRefs

To simplify the work of converting servers to accept requests via both
MBOs and legacy messages, we can temporarily extend CalleesRefs with a
"legacy reply mode": When a legacy request message is read into a
CalleesRef using `zx_msgqueue_wait()`, the CalleesRef will store a
reference to the channel endpoint that the request was written to.
Calling `zx_mbo_send_reply()` on that CalleesRef will then enqueue the
reply as a legacy message so that it is readable from that channel
endpoint.  This feature can be removed after client code no longer
sends requests via legacy messages.

This means that server code will be able to handle legacy requests
without having to be aware of them.  Server code can be converted
directly to using CalleesRefs.  We won't have to add and later remove
server-side code for conditionalising based on whether a request was
an MBO or a legacy message.  The conditionalising only needs to be
done once, in the kernel's IPC implementation, rather than for each
set of FIDL language bindings.

## Alternative transition plan

An alternative transition plan would be to split up the features
provided by the MBMQ model and adopt these features separately.

For example, we could implement MsgQueues, including the ability to
redirect channels to MsgQueues, without implementing MBOs and
CalleesRefs -- or we could add that functionality to Zircon ports.
This would give us the ability to combine `zx_port_wait()` and
`zx_channel_read()` into a single syscall (giving some performance
benefits) and to preserve message ordering across channels.  It would
not give us the other benefits, including shareability of channels,
allowing large messages, and providing memory accounting for messages.

This approach is more incremental and could be easier in some ways,
but it could also make it harder to get to a better overall solution.
