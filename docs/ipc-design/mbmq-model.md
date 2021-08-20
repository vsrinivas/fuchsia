# Message Buffer Message Queue (MBMQ) model for IPC

This is an outline of a set of primitives for IPC (interprocess
communication) proposed for use in Fuchsia.

## Overview of object types

The MBMQ model is based around five types of object:

*   Channel endpoint
*   MsgQueue: message queue
*   MBO: message buffer object
*   CallersRef: the caller's reference to an MBO
*   CalleesRef: a callee's reference to an MBO

A MsgQueue is a queue of messages, or to be more exact, a queue of
MBOs.  A MsgQueue may be set as the destination for a channel
endpoint, so that when a message is sent to the endpoint, it is
enqueued onto the MsgQueue.  Furthermore, one MsgQueue may be set as
the destination for multiple channels.  MsgQueues are a replacement
for Zircon ports.

A channel consists of a pair of channel endpoints.  The destination
MsgQueue for an endpoint can be set using the opposite endpoint of the
pair.  So, for a pair of endpoints A and B, calling
`zx_object_set_msgqueue()` on endpoint B sets the MsgQueue that
messages sent to endpoint A will be enqueued on.

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
    contents, via a CallersRef or a CalleesRef.

*   An MBO acts as a reply path for a request, by which the reply is
    returned from a callee to a caller.  Each MBO may have an
    associated reply-path MsgQueue, which is the queue that the MBO
    will be enqueued on when a callee returns it via
    `zx_mbo_send_reply()`.  This means that a callee does not have to
    specify a channel for returning a reply message on.

A CalleesRef is a callee process's limited-access reference to an MBO.
A callee process can use a CalleesRef to read and write an MBO until
it returns ownership of the MBO back to the caller.  The callee's
access to the MBO is revoked when it sends the reply, so the caller
can then reuse the MBO with a different callee.

Similarly, a CallersRef is a caller process's limited-access reference
to an MBO.  The caller process can use the CallersRef for an MBO to
read and write that MBO while it has ownership of the MBO.

The MBO is a kernel-internal object that userland only access through
CallersRef and CalleesRef handles.  In practice, an MBO and its
CallersRef are implemented by the kernel as the same object on the
kernel heap; there is a one-to-one correspondence between the two and
they have the same lifetime.  However, we use separate terms to
emphasise that a CallersRef handle cannot always be used to read and
write its associated MBO.

## Overview of the request-reply lifecycle

There are four possible states for an MBO, listed here in the order in
which they are typically used:

1.  `owned_by_caller`: Owned by the caller via the CallersRef for the
    MBO: contents accessible through the CallersRef handle
2.  `enqueued_as_request`: Enqueued on a MsgQueue (or a channel) as a
    request
3.  `owned_by_callee`: Owned by a callee via a CalleesRef: contents
    accessible through the CalleesRef handle
4.  `enqueued_as_reply`: Enqueued on a MsgQueue as a reply

An MBO switches between these states as it is sent to a callee,
received, and sent back to the caller.

Figure 1 shows how the MBO state transitions happen for simple usage
of the core syscalls:

*   **Create MBO:** A caller process creates an MBO using
    `zx_mbo_create()`, which returns a CallersRef for the MBO.  The
    MBO starts off in the `owned_by_caller` state.

*   **Send request:** To send a request, the caller process writes the
    request message into the MBO by calling `zx_mbo_write()`, passing
    the CallersRef handle, and then sends the MBO on a channel by
    calling `zx_channel_send()`.  This enqueues the MBO onto the
    channel's associated MsgQueue and switches the MBO's state to
    `enqueued_as_request`.  In that state, the CallersRef handle can
    no longer be used to read or write the MBO, so the caller cannot
    modify the message after it has been sent.

*   **Receive request:** The callee process can read the MBO from its
    MsgQueue using `zx_msgqueue_wait()`, supplying a CalleesRef
    object.  This removes the MBO from the message queue, sets the
    CalleesRef to point to the MBO, and sets the MBO's state to
    `owned_by_callee`.  This state gives the callee the ability to
    read and write the MBO's contents using the CalleesRef handle.
    The caller can read the request message out of the MBO by passing
    the CalleesRef handle to `zx_mbo_read()`.  The caller can write a
    reply message into the MBO (overwriting its contents) by passing
    the CalleesRef handle to `zx_mbo_write()`.

*   **Send reply:** Once the callee has written a reply into the MBO,
    it can send the reply to the caller by invoking
    `zx_mbo_send_reply()` with the CalleesRef handle.  This enqueues
    the MBO on its associated MsgQueue, drops the CalleesRef's
    reference to the MBO (putting the CalleesRef back in the "unused"
    state), and sets the MBO's state to `enqueued_as_reply`.  The
    callee can now reuse this CalleesRef object in later calls to
    `zx_msgqueue_wait()`.

*   **Receive reply:** The caller process can then read the MBO from
    its MsgQueue using `zx_msgqueue_wait()`.  The caller supplies a
    CalleesRef object but in this case the CalleesRef is not used.
    The `zx_msgqueue_wait()` syscall removes the MBO from the MsgQueue
    and sets the MBO's state back to `owned_by_callee`.  The caller
    can use the key value returned by `zx_msgqueue_wait()` to
    determine which MBO was returned, if necessary.  The CallersRef
    handle can now be used to read and write the MBO, so the caller
    can read the reply message from the MBO using `zx_mbo_read()`.

*   **Repeat:** The cycle can now repeat.  The caller can now write a
    new request message into the MBO and send it on a channel as
    above, potentially to a different callee.

---

*Figure 1.* Pseudocode for a simple client and a simple server.  This
shows the basic usage of the core syscalls.  The two are shown side by
side to illustrate how the MBO transitions between states for this
particular interleaving of execution.

```c
// Client (caller)                      | // Server (callee)
                                        |
// Setup                                | // Setup
channel = ...;                          | channel_server_end = ...;
msgq1 = zx_msgqueue_create();           | msgq2 = zx_msgqueue_create();
callersref = zx_mbo_create();           | calleesref = zx_mbo_create_calleesref();
zx_object_set_msgqueue(                 | zx_object_set_msgqueue(
    callersref, msgq1, key);            |     channel_server_end, msgq2, key);
                                        |
// MBO is in state 1                    |
                                        |
// Loop to send multiple requests       | // Loop to handle multiple requests
while (true) {                          | while (true) {
  // Write request message into MBO     |
  zx_mbo_write(callersref, ...);        |
  // Send request on channel            |
  zx_channel_send(channel, callersref); |
                                        |
  // MBO is now in state 2              |
                                        |   // Wait for and unqueue a request
                                        |   zx_msgqueue_wait(msgq2, calleesref);
                                        |
                                        |   // MBO is now in state 3
                                        |
                                        |   // Read request message from MBO
                                        |   zx_mbo_read(calleesref, ...);
                                        |   // Process the request
                                        |   ...
                                        |   // Write reply message into MBO
                                        |   zx_mbo_write(calleesref, ...);
                                        |   // Send the reply
                                        |   zx_mbo_send_reply(calleesref);
                                        |
                                        |   // MBO is now in state 4
  // Wait for and unqueue a reply       |
  zx_msgqueue_wait(msgq1, ...);         |
                                        |
  // MBO is now in state 1              |
                                        |
  // Read reply message from MBO        |
  zx_mbo_read(callersref, ...);         |
}                                       | }
```

---

## Core operations

*   `zx_mbo_create() -> callersref`

    Creates a new MBO and its associated CallersRef and returns a
    handle for the CallersRef.  The MBO starts off in the
    `owned_by_caller` state.

*   `zx_mbo_create_calleesref() -> calleesref`

    Creates a new CalleesRef.  The CalleesRef starts off not holding a
    reference to any MBO.

*   `zx_mbo_read(mbo) -> (data, handles) or (data_size, handle_count)`

    Reads from an MBO.  This reads the entire contents of the MBO.
    (Operations for partial reads will be defined later.)

    This takes an MBO reference, which means either a CallersRef
    handle (for an MBO in the `owned_by_caller` state) or a CalleesRef
    handle (for a CalleesRef pointing to an MBO in the
    `owned_by_callee` state).

    If the message is too large to fit into the buffer passed to the
    syscall, the syscall returns the size of the message.  This allows
    the process to allocate a larger buffer and call the syscall
    again.

*   `zx_mbo_write(mbo, data, handles)`

    Writes to an MBO.  This replaces the MBO's existing contents.
    (Operations for partial/incremental writes will be defined later.)
    This takes an MBO reference (as defined in the description of
    `zx_mbo_read()`).

*   `zx_channel_send(channel, callersref)`

    Sends an MBO as a request on a channel.  Takes a CallersRef for
    the MBO.  The MBO must be in the `owned_by_caller` state.  Its
    state will be changed to `enqueued_as_request`.

    If the channel has an associated destination MsgQueue, as set by
    `zx_object_set_msgqueue()`, the MBO will be enqueued onto that
    MsgQueue and its `current_key` field will be set to the key value
    that was set by `zx_object_set_msgqueue()`.  Otherwise, the MBO
    will be enqueued on the channel so that it is readable from the
    channel's opposite endpoint.

*   `zx_msgqueue_create() -> msgqueue`

    Creates a new MsgQueue.

*   `zx_msgqueue_wait(msgqueue, calleesref) -> key`

    Reads an MBO from a MsgQueue.  If the MsgQueue is empty, this
    first blocks until the MsgQueue is non-empty.

    This takes a CalleesRef as an argument.  The CalleesRef must not
    currently hold a reference to an MBO, otherwise an error is
    returned.

    This removes the first MBO from the message queue.  If the MBO was
    in the `enqueued_as_request` state, this sets the CalleesRef to
    point to the MBO, and changes the MBO's state to
    `owned_by_callee`.

    If the MBO was in the `enqueued_as_reply` state, this changes the
    MBO's state to `owned_by_caller` and does not modify the
    CalleesRef.

    This returns the `current_key` field from the MBO, which allows
    the process to determine which channel the message was sent on
    (for requests) or which request this is a reply to (for replies).

*   `zx_object_set_msgqueue(channel_or_callersref, msgqueue, key)`

    Sets the associated destination MsgQueue for a channel or an MBO.

    For channels: When given channel endpoint 1 of a pair, this sets
    the associated MsgQueue for endpoint 2 of the pair so that calls
    to `zx_channel_send()` on endpoint 2 will enqueue messages onto
    the given MsgQueue with the given key value.  If the channel had
    any existing messages queued on it (previously written to endpoint
    2 and currently readable from endpoint 1), they are moved onto the
    given MsgQueue.

    For MBOs: When given the CallersRef for an MBO, this sets the
    associated MsgQueue for the MBO, onto which `zx_mbo_send_reply()`
    will enqueue the MBO with the given key value.  The MBO must be in
    the `owned_by_caller` state.

*   `zx_mbo_send_reply(calleesref)`

    Returns a reply message to the caller.  The given CalleesRef must
    have a reference to an MBO (which will be in the state
    `owned_by_callee`).  This operation drops the CalleesRef's
    reference to the MBO, and enqueues the MBO onto its associated
    MsgQueue, setting the MBO's `current_key` field to its reply key.
    The MBO's state is set to `enqueued_as_reply`.

    If the MBO had no `reply_queue` set (which can be true for MBOs
    used for fire-and-forget messages), the MBO enters a defunct state
    where it cannot be used any further.  This is equivalent to the
    MBO being enqueued as a reply on a MsgQueue for which the handles
    are later closed.

*   `zx_object_wait_async_mbo(handle, callersref, signals, options)`

    This is a replacement for `zx_object_wait_async()`.  Like that
    syscall, it waits until one or more of the given signals is
    asserted on the object specified by `handle`.  The difference is
    that rather than returning the notification by sending a port
    packet to a port, the new syscall returns the notification as a
    reply on the given MBO.  The notification is returned as if by an
    invocation of `zx_cmd_send_reply()`, enqueuing the MBO onto its
    associated reply queue.

    This means that waiting for a signal on an object is like making a
    call to the object.

    This takes a CallersRef for an MBO.  The MBO must be in the
    `owned_by_caller` state.

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

    Closing the CallersRef handles for an MBO only causes the MBO to
    be freed if there are no other references to the MBO from a
    MsgQueue or a CalleesRef.  If the CallersRef handles for an MBO
    are closed while the MBO is enqueued as a request on a MsgQueue,
    the MBO remains in the MsgQueue, and it can still be read into a
    CalleesRef and sent as a reply, but it will be freed when
    `zx_msgqueue_wait()` returns the MBO to the `owned_by_caller`
    state.

    This means it is possible to do a "fire-and-forget" send with an
    MBO: that is, send the MBO as a request message, but close the
    CallersRef handle and ignore any replies.

*   Automatic replies: An MBO receives an automatic reply message if
    it was sent as a request but there is no way a callee could send a
    reply.  There are two cases for this:

    *   Closed CalleesRef: If a CalleesRef is closed while it holds a
        reference to an MBO, the system will send an automatic reply
        on the MBO.  The system will replace the MBO's contents with a
        default reply message and send the MBO as a reply (as if
        `zx_mbo_send_reply()` was called).

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

*   MsgQueue: When all the handles to a MsgQueue are closed, any
    messages on the MsgQueue's queue are removed from the queue.
    There may still be kernel-internal references to the MsgQueue
    (from MBOs' `reply_queue` fields and from channels), but these are
    not sufficient for keeping the messages on the queue because these
    references cannot be used for reading from MsgQueues.

## State for each object type

This section gives a summary of the state that is stored by each of
the object types.

MBO:

*   Message contents.  This consists of two resizable arrays:
    *   An array of bytes (data).
    *   An array of Zircon handles.
*   `current_key`: 64-bit integer.  This is set when the MBO is
    enqueued onto a MsgQueue by either `zx_channel_send()` or
    `zx_mbo_send_reply()`.  Its value is returned by
    `zx_msgqueue_wait()`.
*   `reply_queue`: This is the MsgQueue that `zx_mbo_send_reply()`
    will enqueue the MBO onto when it is sent as a reply.
*   `reply_key`: 64-bit integer.  `zx_mbo_send_reply()` will set the
    MBO's `current_key` field to this value when the MBO is sent as a
    reply.
*   State: one of the four MBO states listed above (`owned_by_caller`,
    `owned_by_callee`, `enqueued_as_request`, `enqueued_as_reply`).
    Note that in practice we do not need to distinguish between
    `enqueued_as_request` and `owned_by_callee`.  Operations on
    CallersRef handles need to check for `owned_by_caller`, whereas
    `zx_msgqueue_wait()` needs to check for `enqueued_as_request`
    versus `enqueued_as_reply`.

CallersRef:

*   A CallersRef needs no state of its own.  A CallersRef can be
    represented as just a reference to an MBO, or it can be
    implemented as the same heap object as the MBO.

CalleesRef:

*   Reference to an MBO.  This reference may be null.  If the
    reference is non-null, the MBO is in the `owned_by_callee` state.

MsgQueue:

*   List of MBOs, all of which will be in the state
    `enqueued_as_request` or `enqueued_as_reply`.  This can use an
    intrusive list implementation so that adding an MBO to the list
    does not require doing a memory allocation.

Channel endpoint:

*   Reference to a MsgQueue.  This reference may be null.
*   `channel_key`: 64-bit integer.  When an MBO is sent through this
    channel endpoint, its `current_key` field will be set to this
    `channel_key` value.
*   List of MBOs, all of which will be in the state
    `enqueued_as_request`.  This will be empty if the endpoint has an
    associated MsgQueue.

## Combined send+wait operation

The core IPC operations described above can all be invoked via
separate syscall invocations.  In addition to those syscalls, we
provide a combined send+wait syscall that allows a specific sequence
of those core IPC operations to be done in a single syscall
invocation.  This allows a process to send an outgoing message and
then wait for an incoming message.

Using this combined syscall reduces the overhead associated with
syscall invocations that comes from entering and leaving kernel mode.
More importantly, it allows the kernel to optimise the cases where it
is possible to do a direct context switch to the receiver process.  If
the "send message" step wakes a thread, and if the
`zx_msgqueue_wait()` step would block, the kernel can switch directly
to the thread that was woken.

Furthermore, if the message being sent fits into the buffer provided
by the receiver, the kernel can potentially copy the message directly
to the receiver's buffer without making an intermediate copy in the
MBO's buffer.  This is termed the "direct-copy optimisation".  (Note,
however, that this is not entirely straightforward to implement,
because the sender and receiver's address spaces will usually not be
mapped at the same time.)

### Definition

```c
struct zx_mbmq_multiop {
  // Inputs for write+send:
  bool is_req;            // true if sending a request, false if sending a reply
  zx_handle_t mbo;        // for zx_mbo_write() + zx_channel_send()/zx_mbo_send_reply()
  zx_handle_t channel;    // for zx_channel_send() (if is_req is true)

  // Inputs for wait+read:
  zx_handle_t msgqueue;   // for zx_msgqueue_wait()
  zx_handle_t calleesref; // for zx_msgqueue_wait()

  buffer_info buf;        // for zx_mbo_write() and zx_mbo_read()

  // Output:
  uint64_t key;           // from zx_msgqueue_wait()
};

zx_status_t zx_mbmq_multiop(zx_mbmq_multiop* args);
```

`zx_mbmq_multiop()` does the following:

*   Do `zx_mbo_write()` to write the message specified by `buf` into
    the MBO specified by `mbo` (which may be a CallersRef or
    CalleesRef handle).
*   Send message:
    *   If `is_req` is true, do `zx_channel_send()` to send `mbo` on
        `channel`.
    *   If `is_req` is false, do `zx_mbo_send_reply()` on `mbo` to
        send the message as a reply.
*   Do `zx_msgqueue_wait()` on `msgqueue` and `calleesref`.  Returns
    the resulting key value in `key`.
*   Do `zx_mbo_read()` to read the message from the MBO that was
    unqueued by `zx_msgqueue_wait()` into `buffer`.
    *   If the message was fully read into the buffer, the MBO is
        truncated (i.e. its copy of the message is dropped).  This is
        to allow the direct-copy optimisation.
    *   If the message that was unqueued was a request, this is
        equivalent to `zx_mbo_read()` on `calleesref`.  Otherwise, if
        the unqueued message was a reply, then if userland were to do
        an equivalent `zx_mbo_read()` call it would involve looking up
        the CallersRef handle based on the `key` value.

## Properties of CalleesRefs

CalleesRefs have these useful properties:

*   **Acts as a reply capability:** A CalleesRef acts as single-use,
    revokable capability for replying to a request.  When the reply is
    sent, the CalleesRef's reference to the MBO is dropped, revoking
    the callee's ability to use it to modify the MBO or send the MBO
    as a reply again.

*   **Reusable:** A callee can reuse a CalleesRef across multiple
    requests.  This means we can avoid doing an allocation and
    deallocation for each request, and we can avoid modifying the
    handle table for each request.  (A CalleesRef's ability to reply
    to a particular request is revoked when the reply is sent, but the
    CalleesRef itself is not revoked.)

*   **Acts as a message handle:** A CalleesRef acts as a handle to a
    request message.  This means that a large request can be read
    incrementally by doing multiple syscall invocations using that
    handle to read parts of the message.  (Note, however, that we have
    not defined the syscalls for doing that yet.)  This means that a
    careful callee can potentially accept arbitrarily large messages
    while avoiding being vulnerable to memory exhaustion DoS.

    In contrast, Zircon's current `zx_channel_read()` syscall requires
    that a message be read fully into memory (or be truncated).  This
    means that if the current 64k message size limit were removed,
    there would be no way for a message receiver to use
    `zx_channel_read()` to receive an arbitrarily large message
    without risking exhausting its own memory.

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
    for each fire-and-forget message.  The caller closes the
    CallersRef handle after sending the MBO, without ever setting a
    `reply_queue` on the MBO.  The MBO will get freed automatically
    after the callee has received the message and dropped its
    reference to the MBO.

*   Recycled MBOs: A callee has the option of detecting when the
    callee has dropped its reference to the MBO.  It can exercise that
    option by setting a `reply_queue` on the MBO in order to receive a
    reply, just as with MBOs where non-trivial replies are expected.
    This may be useful for a resource-conscious caller, which can use
    this ability to recycle MBOs between requests or to implement flow
    control.

For fire-and-forget requests, a well-behaved callee should release the
MBO after it has read or processed the message by writing an empty
reply message into the MBO and calling `zx_mbo_send_reply()` (or,
equivalently, by closing the CalleesRef handle).  Unfortunately that
has the problem of requiring an extra syscall invocation, so we might
want to introduce a way of releasing the callee's MBO reference
implicitly when the message is read.

Note that a badly-behaved callee could hold into the MBO indefinitely,
but that is not very different from behaving badly by never unqueuing
requests.

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

### FIDL events

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
IPC primitives is that the relative ordering of the request and event
messages would no longer be preserved.  It is possible that some code
uses FIDL event messages (rather than using request messages on a
separate channel) for that reason.  However, the MBMQ model allows the
message ordering to be preserved across channels, as described below.

## Preserving message ordering across channels

The MBMQ model is able to preserve the ordering of messages sent on
different channels that are handled by the same server process.  This
means that, for example, if message M1 is sent on channel C1 and then
message M2 is sent on channel C2, the server process can ensure that
the messages are processed in the order they were sent.  The server
just has to ensure that C1 and C2 have the same MsgQueue set as their
destination, which will preserve the message ordering within the
MsgQueue.

In contrast, Zircon's current IPC primitives are not able to preserve
message ordering in this case, because channels C1 and C2 have
separate message queues.  As messages are enqueued onto those queues,
the information about their interleaving is lost.  Zircon currently
preserves message ordering only within a channel, not between
channels.

## Transition plan

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

### Legacy reply mode for CalleesRefs

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

### Alternative transition plan

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

## Notes on terminology

### Role of the "key" values

In this document, the term "key" has essentially the same meaning as
in Zircon's current [`zx_object_wait_async()`][zx_object_wait_async]
and [`zx_port_wait()`][zx_port_wait] syscalls.

[zx_object_wait_async]: <https://fuchsia.googlesource.com/fuchsia/+/dc596b81547a0930c88945bff32c8094a361ba3c/docs/reference/syscalls/object_wait_async.md>
[zx_port_wait]: <https://fuchsia.googlesource.com/fuchsia/+/dc596b81547a0930c88945bff32c8094a361ba3c/docs/reference/syscalls/port_wait.md>

In the MBMQ model, a key value is used by a process to identify which
of its incoming channels a message came from, or which of its earlier
requests an incoming reply corresponds to.  A process can use whatever
key values it wants when passing them to `zx_object_wait_async()` or
`zx_object_set_msgqueue()`.  We expect that the typical usage will be
for a process to treat a key as being a pointer to some data structure
in its address space.

### "Caller" and "callee"

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
