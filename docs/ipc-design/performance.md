# MBMQ: Performance of IPC

See the [contents page](index.md) for other sections in this document.

This section describes some performance problems with Zircon's current
IPC mechanisms and explains how the MBMQ IPC model can address those
problems.

[TOC]

## Introduction

Zircon IPC has a number of inefficiencies, which we discuss below.  In
analysing this, we mostly consider the case of using channels with
ports, because that is the most general case, since it can be used in
async event loops.  Figure 1 shows some example usage.  We also
discuss how other operations, such as `zx_channel_call()`, behave
differently.

---

*Figure 1.* Pseudocode for a simple client and a simple server using
Zircon's current IPC primitives, in the general case of using channels
with ports.  The two are shown side by side to illustrate one possible
interleaving of execution.

```c
// Client (caller)                      | // Server (callee)
                                        |
// Setup                                | // Setup
channel = ...;                          | channel = ...;
port = zx_port_create();                | port = zx_port_create();
                                        | // Register for first request
                                        | zx_object_wait_async(channel, port,
                                        |                      ...);
                                        |
// Loop to send multiple requests       | // Loop to handle multiple requests
while (true) {                          | while (true) {
  // Register for reply                 |
  zx_object_wait_async(channel, port,   |
                       ...);            |
  // Send request                       |
  zx_channel_write(channel, ...);       |
                                        |   // Wait for request
                                        |   // (or other message)
                                        |   zx_port_wait(port);
                                        |
                                        |   // Read request
                                        |   zx_channel_read(channel, ...);
                                        |   // Re-register for next request
                                        |   zx_object_wait_async(channel, port,
                                        |                        ...);
                                        |   // Process the request
                                        |   ...
                                        |   // Write reply
                                        |   zx_channel_write(channel, ...);
  // Wait for reply (or other message)  |
  zx_port_wait(port);                   |
  // Read reply                         |
  zx_channel_read(channel, ...);        |
}                                       | }
```

---

## Problem 1: Overhead of "wake-then-wait" usage

One problem may be apparent from looking at the pseudocode in
Figure 1.  The number of runnable threads is not a constant, and can
switch between 1 and 2.

For example, if the server thread is blocked in `zx_port_wait()` when
the client does it `zx_channel_write()` call, the number of runnable
threads will go from 1 to 2, because the server thread will become
newly runnable while the client thread stays runnable.  If the client
thread continues running, it will immediately block on
`zx_port_wait()`, and the number of runnable threads will go back to
1.

This is a general problem that arises whenever we have
"wake-then-wait" behaviour, where a thread wakes another thread and
then waits, as two separate operations.

In these cases, the scheduler has a choice between:

1.  Using another CPU to run the newly-runnable thread.
2.  Not using another CPU: either switching to the newly-runnable
    thread, or deferring the wakeup and continuing to run the current
    thread.

Neither is ideal:

1.  If we choose to use another CPU to run the newly-runnable thread,
    this involves some delay from the IPI latency (the time taken to
    signal and wake up the other CPU).  There may also be lock
    contention as both CPUs try to access the same channel and port
    data structures concurrently.

2.  If we choose not to use another CPU, this will tend to require
    that the scheduler's thread selection algorithm is run one or more
    times during an IPC round trip.  This is not ideal because this
    algorithm is relatively expensive (especially if the system has
    many other runnable threads).

The scheduler is also not in a good position to choose between these,
because, at the point where the thread wakes another thread, the
scheduler doesn't know that the thread is going to wait immediately
afterwards.

The problem can be addressed by providing combined send-and-wait
operations.  This allows the kernel to switch directly to the woken
thread on the same CPU (in the cases where that performs better than
waking the thread on another CPU).  This allows us to reduce the
amount of scheduler decision-making code that is run.

Zircon does provide `zx_channel_call()` as a send-and-wait operation
that the client side can use, but there is no corresponding
send-and-wait operation that the server side can use, so we end up
with wake-then-wait behaviour when the server sends its reply to the
client.

Zircon could address that by providing a somewhat ad-hoc combined
`zx_channel_write()` + `zx_port_wait()` operation, but combining this
with `zx_channel_read()` to further reduce syscall overhead is more
difficult -- see the "Message doubling" section below.

The MBMQ model addresses this by providing a general send-and-wait
that both the client and server can use.  However, a caveat is that
making full use of that may require changing usage patterns in
async-event-loop-based code.

## Problem 2: Message doubling: Two messages are required to send one

In Zircon IPC's general case of using channels with ports, sending a
single message actually involves sending two messages: sending a
message on a channel causes a message to be enqueued on the receiver's
port.

This doubles some of the bookkeeping work that is required: Two
messages must be queued and unqueued from lists, not just one.  Two
allocations and deallocations are required, not just one (though the
allocation for registering an async wait on a port is reused for the
port message).

This "message doubling" in Zircon IPC has a further problem in that it
makes it difficult to combine the wait and read operations
(`zx_port_wait()` and `zx_channel_read()`) into one.  A port message
doesn't maintain an in-kernel reference to the channel that triggered
it.  Given that, the kernel can't provide a combined
`zx_port_wait()` + `zx_channel_read()` syscall.  It is required for
userland to get the "key" value from the port message returned by
`zx_port_wait()`, then look that up in its own data structures to find
the corresponding channel handle, and pass that to
`zx_channel_read()`.

The MBMQ model avoids this problem by allowing channels to be
redirected to MsgQueues, so that channel messages are enqueued onto
MsgQueues.  This addresses both problems: it reduces the amount of
bookkeeping, and it allows for a combined wait+read.

## Problem 3: Syscall count

The general case of using channels with ports, as shown in Figure 1,
requires 4 syscall invocations on each side, for 4 separate steps
(register, wait, read, and send).  This incurs more overhead from
syscall invocations than is ideal.  (Note that the syscall overhead is
higher when mitigations for CPU problems like Meltdown and Spectre are
enabled.)  Ideally we would only use 1 syscall invocation on each side
(or 2 for the cases where it is difficult to separate the send and the
wait).

Zircon does allow using 1 syscall invocation on the client side via
`zx_channel_call()`, but there is no equivalent on the server side.
It is possible to reduce the syscall count from 4 to 3 by using
`zx_object_wait_one()` or `zx_object_wait_many()` instead of
`zx_port_wait()` + `zx_object_wait_async()`, but these have limits on
how many channels they can wait on.

The MBMQ model reduces the syscall count by providing a syscall that
combines send+wait+read, and by avoiding the need for the "register"
step (re-registering the async wait with `zx_object_wait_async()`).

The MBMQ model makes it straightforward to combine the wait and read
steps into a single operation, because they both operate on the same
object (a MsgQueue).  It is not straightforward to combine these
operations with Zircon's current channel and port mechanisms because
of the problem described above in the "Message doubling" section.

## Problem 4: Memory allocations

For the general case of using channels with ports, two
allocation+deallocation pairs are required for each message transfer:

*   Allocation of the channel message.  The message buffer and the
    list node are combined into the same allocation.  This is
    currently allocated from a channel-specific pool.
*   Allocation of the `zx_object_wait_async()` registration.  This
    allocation is reused for the port message.  This is currently
    allocated using the general purpose allocator (`malloc()`).

Allocation and deallocation generally involve taking a global lock, so
this would translate to 8 lock+unlock pairs per IPC round trip.  Using
a general purpose allocator can be relatively expensive because it may
involve searching a freelist for a suitably sized block.

The MBMQ model allows these per-message allocations to be avoided.

The allocation for the async wait registration and port message is
avoided because MBMQ model does not require the wait to be
re-registered for each message, and because the role of the port
message is merged into the channel message (as discussed above).

The allocation for the channel message can be avoided because an MBO
can be reused across multiple message transfers.  This assumes that we
provide an operation on MBOs for preallocating a message buffer that
is large enough to be used across those transfers (analogous to
`reserve()` on a C++ `std::vector`).

To get the full benefit from this, we would need to ensure that the
cost is not just moved elsewhere.  For example, if userland were to
cache MBOs in a process-global pool, this would potentially just
impose similar costs, only in userland rather than in the kernel.
More intelligent reuse of MBOs, however, could reduce the overall
costs.

## Problem 5: Message copying

With Zircon IPC, each channel message must be copied twice by the
kernel:

1.  From the sender process into a kernel message buffer.
2.  From the kernel message buffer into the receiver process.

Clearly this becomes more significant for larger messages.

It would be better if we only needed to copy the message once, from
the sender process to the receiver process.  There are two ways this
can be addressed with the MBMQ model.

With an extension to the basic MBMQ model, we can allow a receive
buffer to be registered with a MsgQueue, so that when a message is
sent on a channel and redirected to that MsgQueue, the kernel will
copy the message contents directly to the receive buffer.

Without that extension, the kernel can copy directly from the sender
to the receiver only in the case where a receiver is already waiting
on the destination MsgQueue.  This is the "direct-copy optimisation"
covered in the [main description](mbmq-model.md).
