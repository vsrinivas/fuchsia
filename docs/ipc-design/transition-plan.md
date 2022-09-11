# MBMQ: Transition plan

See the [contents page](index.md) for other sections in this document.

This section outlines a transition plan for implementing the MBMQ
model in Fuchsia.

[TOC]

## Two-way interoperation between legacy messages and MBO messages

As a transitional measure, we can extend the MBMQ model so that legacy
programs and MBO-aware programs can interoperate.

This means that:

*   Legacy messages (those sent with `zx_channel_write()`) can coexist
    on the same channel with MBO messages (those sent with
    `zx_channel_send()`).
*   Legacy clients can send requests to MBO-aware servers and get
    replies back.
*   MBO-aware clients can send requests to legacy servers and get
    replies back.

Allowing interoperability in both directions in this way should make
the transition easier: it allows for a more gradual transition.

We can implement the interoperability support in the kernel, which
means that MBO-aware programs will not need to be aware of legacy
messages.

### Case 1: Legacy client, MBO-aware server

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

### Case 2: MBO-aware client, legacy server

The case of handling interoperation so that an MBO-aware client can
send requests to a legacy server would be implemented in the kernel
using reply routing logic that is similar to what is implemented for
`zx_channel_call()` already.

In this case, an MBO-aware client uses `zx_channel_send()` to send an
MBO-based request message to a server.  The server reads the request
using `zx_channel_read()`.  This use of the legacy `zx_channel_read()`
call to read an MBO-based request is treated specially by the kernel.
It reads the `txid` (transaction ID) field from the message, and if
`txid` is non-zero, it records (in the channel data structure) that
any reply sent on the server's channel endpoint with that `txid` with
`zx_channel_write()` should be routed back to the MBO.

This relies on the current property that `txid` is zero for send-only
FIDL request messages (requests where no reply is expected).  When
`txid` is zero, the kernel will not store a back-reference to the MBO
in the channel.  The reason for this is that if the kernel did store
back-references for send-only messages in the channel, this would be a
memory leak: they would accumulate and never be freed until the
channel was freed.

## Transition steps

*   First, implement the new kernel primitives, including MsgQueues,
    MBOs, CallersRefs and CalleesRefs.  MsgQueues will be added as a
    new object type, separate from Zircon ports.  The two-way
    interoperation described above will be implemented.  Channels will
    be modified so that they can contain both legacy messages and MBO
    messages.

*   We then have flexibility in the order in which we convert code to
    using the MBMQ primitives.  We will need to:

    *   Switch processes over to using MsgQueues instead of Zircon
        ports.  An initial conversion can replace uses of
        `zx_object_wait_async()` with `zx_object_wait_async_mbo()`,
        but a later version should use `zx_object_set_msgqueue()` for
        waiting on channels, which should give some performance
        improvements.

    *   Change server code to accept requests via MBO messages.

    *   Change client code to send requests via MBO messages.

*   Cleanup: Once the codebase has been converted to using MBO
    messages, various things can be removed:

    *   The kernel's support for legacy messages can be removed.  The
        interoperation support can be removed.  The `txid` mapping on
        channels, used for `zx_channel_call()` and for legacy message
        interoperation, can be removed.

    *   Zircon ports can be removed from the kernel.

    *   The transaction ID (`txid`) field can be removed from FIDL
        messages, because we no longer need it for matching up replies
        with requests or for distinguishing send-only requests.

See ["Shareable channels"](shareable-channels.md) for some further
steps that could be taken if we decided to make channels
unidirectional and shareable.

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
