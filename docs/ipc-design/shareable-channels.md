# MBMQ: Shareable channels

See the [contents page](index.md) for other sections in this document.

This section discusses the possibility of switching Fuchsia from using
bidirectional (non-shareable) channels to unidirectional (shareable)
channels.

[TOC]

## Bidirectional channels versus shareable channels

Currently, Zircon channels are bidirectional.  FIDL uses one direction
for request messages and the other direction for reply and event
messages.

Using bidirectional channels this way has the disadvantage that it
causes channel endpoints to be *non-shareable*.  If the client
endpoint of a channel were to be shared between multiple processes (by
duplicating the handle), the processes would run into a problem when
attempting to send requests on that endpoint: There would be no way to
route the reply for a request back to the process that sent the
request.  If the processes attempted to read replies, they would race
to receive each other's replies from the same channel queue.  (This
problem is solved for requests sent using `zx_channel_call()`, but not
for other requests.)

As a result, Zircon disallows duplicating channel handles, to prevent
processes from getting into a situation where replies are misrouted in
that way.

The non-shareability of channels has some disadvantages:

*   Non-shareability complicates matters when we want to share a FIDL
    object between multiple processes.

    One option here is for a FIDL protocol to provide a `Clone` or
    `Duplicate` method for creating a new channel referencing the same
    FIDL object.  Examples of this are `fuchsia.io.Node/Clone`,
    `fuchsia.ldsvc.Loader/Clone` and
    `fuchsia.sysmem.BufferCollectionToken/Duplicate`.  This requires
    the server process to co-operate in making the reference
    shareable.  This approach is somewhat problematic in cases where
    we want strong protections against memory exhaustion DoS, because
    a `Clone` method must allocate data structures in the server
    process.

    Another option is to acquire multiple references to a FIDL object
    from the source that we get the object from, e.g. by doing
    multiple `Open` calls on a `fuchsia.io.Directory`.

    A further option is to create proxy channels that forward requests
    to one FIDL object.  This is clearly undesirable for performance
    reasons.

*   Non-shareability also complicates matters when we want to share a
    FIDL object between multiple threads or other entities in the same
    process.

    If channel handles were shareable, we could make duplicates of a
    channel handle and hand them off to different entities within a
    process, allowing the lifetime of each duplicate handle to be
    managed separately.  When channel handles are non-shareable, this
    can necessitate more complicated protocols for ownership and
    thread safety such as reference counting or locking.

In contrast, the MBMQ model allows channels to be made shareable,
because it provides per-request paths through which reply messages can
be sent.

At the same time, the MBMQ model is compatible with bidirectional
channels.

## FIDL events

FIDL event messages are sent on Zircon channels in the same direction
as reply messages.  If we wanted to change channels to be
unidirectional and shareable, we would need to change users of FIDL
events to send event messages on a separate channel.  We could
implement that in the FIDL bindings to avoid the need to modify users
of FIDL.

As an example, consider this FIDL protocol:

```
protocol Foo {
    Method(Arg arg) -> ();
    -> OnEvent1(Arg arg);
    -> OnEvent2(Arg arg);
};
```

That could be implicitly converted to this:

```
protocol Foo {
    Method(Arg arg) -> ();

    GetEventStream() -> (request<FooEventCallback>);

    // Alternative version:
    //   SetEventCallback(FooEventCallback callback);
};

protocol FooEventCallback {
    OnEvent1(Arg arg);
    OnEvent2(Arg arg);
};
```

The use of `request<FooEventCallback>` as a return type in
`GetEventStream` allows the `Foo` server to queue up event messages on
the `FooEventCallback` channel in advance, before the client calls
`GetEventStream`.  The alternative version, `SetEventCallback`, does
not allow that.

One reason for not applying this transformation with Zircon's current
IPC primitives is that the relative ordering of the reply messages and
event messages would no longer be preserved.  It is possible that some
code uses FIDL event messages (rather than using request messages on a
separate channel) for that reason.  However, the MBMQ model allows the
message ordering to be preserved across channels, as described in
["Preserving message ordering"](mbmq-model.md).
