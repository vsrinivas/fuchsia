# The Demultiplexing Principle

Just as a kernel is responsible for isolating programs from one another,
protecting each program's memory, CPU budget, etc from interference by other
programs, a network stack is responsible for isolating programs' network
resources, protecting them from interference by other programs.

More generally, we can say that a network stack is responsible for isolating the
resources of different *security domains*, where a security domain might be a
process, a collection of processes (for example, a collection of processes owned
by the same user on a UNIX system), a sandbox within a process (for example, a
web assembly program run inside of a VM which runs multiple such programs at a
time), etc.

Isolation of network resources is a security property. Imagine a security
domain, A, that initiates a TCP connection with a remote server, and accepts
instructions over that connection. If another domain, B, on the system is able
to inject data into the TCP stream, then an important security property of
domain A has been violated.

Since all interaction with the network stack (excepting management plane
interaction, which is not relevant to this discussion) is via sockets, this
implies that the stack is responsible for isolating sockets from each other. A
given security domain may opt to allow two or more of its sockets to interfere
with each other since that doesn't violate that domain's security, but this must
not be the default, at least for sockets owned by different domains.

We define "interference" here as one socket either receiving traffic that was
meant for another one, or causing traffic that should be received by it to be
received by a different socket instead. In other words, when a remote host
receives a packet and then responds with a packet of its own, it intends for the
response to be received by the original sender. Conversely, when a remote host
sends a packet unprompted, it intends for that packet *not* to appear as though
it is the response to some previously-sent packet. A socket has "interfered"
with another socket if either of these properties don't hold.

Outbound traffic from multiple sockets is multiplexed together into a single
outbound stream of IP packets or link-layer frames. Inbound traffic - in the
form of a single stream of IP packets or link-layer frames - is demultiplexed in
order to deliver each packet to the appropriate socket. Different sockets may
employ different demultiplexing schemes (for example, TCP and UDP use the
four-tuple of remote and local IP address and remote and local port, while ICMP
echoes use the three-tuple of remote and local IP address and ICMP ID), but the
fundamental requirement of being able to demultiplex a single stream of inbound
traffic into multiple different sockets remains the same.

Together, these security requirements and the multiplexed nature of network
traffic imply what we refer to as the **demultiplexing principle**. The
demultiplexing principle states:

    1. Every socket is associated with a set of packet addresses such that it
       would be valid for the socket to receive a packet with any of the
       addresses in that set. This is called the socket's "address set".
    2. No two sockets may exist at the same time whose address sets overlap.
    3. A socket must never send a packet that would prompt a response whose
       address is not in the socket's address set.

    If two sockets, A and B, are owned by the same security domain, then the
    rules are relaxed as follows:
    4. Sockets A and B may exist at the same time even if their address sets
       overlap so long as the owning security domain has opted into it.

Note that (3) depends on the semantics of the protocol in question.

If we follow rules (1) through (3), then we can guarantee two things. First, we
can guarantee that it can never be ambiguous which socket to deliver a packet to
(if it were ambiguous, it would imply that rule (2) had been broken). Second, we
can guarantee that, given sockets A and B, socket A can never interfere with
soket B by sending traffic whose response will be delivered to socket B (if it
could, this would imply either that the response traffic could be delivered to
either A or B, violating rule (2), or that the traffic was unambiguously
destined to socket B, violating rule (3)).

Consider what might happen if we don't abide by this principle. We might have
two UDP sockets - socket A with local address `1.1.1.1:1234` and remote address
`*:*`, and socket B with local address `1.1.1.1:1234` and remote address
`2.2.2.2:5678`. If an inbound packet is received from `2.2.2.2:5678` to
`1.1.1.1:1234`, which socket should it be delivered to? It's possible that the
remote host is attempting to communicate with socket A. It's also possible that
socket B previously sent a packet, and the remote host is responding to it.
There's no way to tell which is the case, and so it's ambiguous which socket
should receive the packet.
