# Control Flow

This document describes how control flow works in the netstack.

Control flow in the netstack is event-driven. The netstack defines various
"workers", each responsible for consuming a specific type of event and observing
the event's source directly. That worker is responsible for processing the event
in its entirety, including updating any state and emitting any other events in
response to the incoming event.

There are three types of events in the system:
- Incoming packets from the network
- Incoming requests from local application clients (open new connection, read on
  an existing connection, etc)
- Timers firing (a timer fire event may include associated data, the type of the
  timer (e.g. "TCP ACK timeout"), etc)

The netstack may emit three types of events:
- Outgoing packets to the network
- Responses to requests from local application clients (return newly-created
  connection, return data for a read request on an existing connection, etc)
- Installing timers

Once an event has been fully processed, the code responsible for its processing
returns, and the worker goes back to blocking for a future event.

Consider the following example of an event being handled:
1. The netstack receives an Ethernet frame from the Ethernet driver. This causes
   the Ethernet worker loop to become unblocked.
2. The Ethernet worker executes the function responsible for processing incoming
   Ethernet frames.
3. This function parses the frame, and discovers that it contains an IPv4
   packet. It passes control to the function responsible for processing incoming
   IPv4 packets.
4. This function parses the packet, and discovers that it contains a TCP
   segment. It passes control to the function responsible for processing
   incoming TCP segments.
5. This function parses the segment, and discovers that it is a SYN from a
   remote host attempting to initiate a new TCP connection. It finds the
   appropriate local TCP listener, and creates a new TCP connection object to
   track the new connection. It emits the following events:
   - It sends a SYN/ACK segment back to the sender of the SYN segment.
   - It sends a message to the local application client which created the TCP
     listener to inform it that there's a new connection.
   - It installs a timer which will fire if the remote side doesn't respond with
     an ACK within a certain period of time.
6. This function is done, so it returns. None of the parent functions (the one
   for processing incoming IPv4 packets or the one for processing incoming
   Ethernet frames) have any more work to do, so they return as well.
7. The Ethernet worker returns to its steady state of waiting for further events to
   process.

This control flow design has a number of advantages:
- Since each event results in a single function call, the flow of control
  within the core of the netstack is easy to follow, and follows normal
  function call flow. There's no indirect flow, for example in the form
  of scheduling and later executing callbacks or other event processing.
- From a development perspective, the simplicity of control flow makes it easy
  to iterate and add features without having to think carefully about which
  logic is performed on which threads, having to do complex refactors to
  reorganize how logic is assigned to threads, etc.

Currently, all workers use a shared common core state to operate, which is kept
behind a futures-aware mutex. Thread contention is not a problem in the current
state because the stack is single threaded. Future expansions are planned to
split core state into smaller states owned by each worker to decrease the
surface for contention and allow a multi-threaded approach, see
[`IMPROVEMENTS.md`](./IMPROVEMENTS.md#single-threaded-execution) for more on
this topic.

While a single-threaded architecture is the right one for us during early
development, we will likely move away from it eventually as we focus more on
performance.
