# Overnet Architecture

## Overview

Overnet provides a mesh network with a service discovery mechanism that can forward around FIDL communications.
This document serves as a rough architectural map and a glossary of terms that have special meaning in Overnet.

## Major Concepts

### Meshes and Nodes

An Overnet mesh is a collection of nodes belonging to a single entity.
Nodes are addressable units within a mesh, and are coupled with an address known as a NodeId.
In the current implementation of Overnet, NodeId's are randomly assigned at node instantiation time, but this may change as the authentication story becomes more complete.

Within the Overnet core library, Node's are created by creating a [`Router`](/src/connectivity/overnet/lib/core/src/router/mod.rs) instance.
Upon instantiation a node cannot communicate with other nodes in the mesh. To do so, a [Link](#links) must be added to some other node.

Each node exports a list of services that may be connected to from other nodes.

Each node tracks a list of [peer nodes](#peers) that it can route to.

### Links

An Overnet Link provides connectivity between two nodes.
No particular transport technology is mandated for a Link, indeed it's expected that a single mesh include multiple link technologies between nodes.
To keep Link implementation simple, all a link needs to do is provide a mechanism to transport up to 1400 byte datagrams unreliably.
Unreliably here means some packets may be dropped, repeated, reordered, or corrupted, but the majority should be transmitted successfully.

Links are typically implemented externally to the Overnet core library.
This provides some convenient implementation firewalls, and allows Overnet embedders to compose different suites of protocols more easily, meaning not every technology needs to be ported simultaneously!
Currently the following link transports are implemented:
- [UDP](/src/connectivity/overnet/lib/udp_link/src/lib.rs) - embeds Overnet datagrams within [QUIC](#quic) datagrams (using QUIC for authentication, encryption, and flow control), and transports said QUIC datagrams over UDP sockets.
- [Serial](/src/connectivity/overnet/lib/serial_link/src/lib.rs) - allows Overnet datagrams to be transported over serial links.
Provides a protocol that allows multiplexing log lines and Overnet frames.
- Socket - provides a link implementation over a Zircon Socket.
This in turn provides a mechanism for Overnet links to be transported over anything that provides a reliable byte stream, for instance a TCP connection.
FFX uses this link mechanism to transport Overnet frames over SSH links between host and target.
Implemented in both [hoist](/src/connectivity/overnet/lib/hoist/src/not_fuchsia.rs) and [overnetstack](src/connectivity/overnet/overnetstack/src/main.rs).
- Unix domain sockets - ascendd provides a Unix domain socket for host instances of Overnet to connect to.

Link frames come in two categories - [messages](#message-frames) and [control](#control-frames).
Additionally link frames can contain some lightweight [additional payload](#additional-frame-payloads).

Link frames carry a label that is not defined in FIDL: it cannot be as FIDL negotiation cannot occur before the header is sent.
Details of this serialization are contained in [frame_label.rs](src/connectivity/overnet/lib/core/src/link/frame_label.rs).
Essentially, we have a one byte bit field contains which fields are present, and then fields are encoded as needed along side it.
One interesting point for this label is that it's encoded in reverse at the end of the frame.
This has the advantage of simplifying the encoding pipeline by not needing to inject bytes at the beginning of the frame, necessitating either complex logic or a copy.

#### Control Frames

Link control frames are not routable - they can only be sent to the links direct peer node.

Control frame payloads are defined by [LinkControlFrame](/sdk/fidl/fuchsia.overnet.protocol/link_protocol.fidl).
Control frames are sent using a simple protocol that requires an ack for each datagram sent before the next one can be - as such the control protocol is inappropriate for bulk data transfer.

Currently control frames are used to send an initial introduction frame, allowing link parameters to be set, and to exchange [routing](routing) information.

Until the first control frame is acked, control frames transport the source node id.
This is how the peer node id for a link is discovered.
After the ack, control frames omit sending the source node id to save some space.

#### Message Frames

Link message frames include source and destination node ids.
If the source node id is the sender of a message, or the destination node id is the peer of a link, that node id can be elided in the protocol.
This provides some substantial bandwidth savings in common cases.

#### Additional Frame Payloads

Link frames can be annotated with some additional side band information.
- PING: A frame can be marked as a ping with a ping identifier.
Doing so requires the peer to send a frame marked PONG as soon as is possible.
- PONG: A frame marked with PONG includes the requesting PING identifier, for correlation.
It also includes an approximation of how long the PONG took to solicit.
- DEBUG_TOKEN: A frame can optionally carry a random number with it.
This has proven useful in correlation of debug statements between sender and receiver in the past.
Note that it's relatively high cost and so probably should not be used in production builds.

The PING/PONG mechanism allows link latency to be measured, which provides a good metric for route selection.

### Peers

Each node in the mesh constructs a Peer node to every other node.
This Peer node consists of a client [QUIC](#quic) connection.
Consequently, each node in the mesh also carries at least one server QUIC connection to every other node.
More server QUIC connections may exist briefly due to stale connections.
Note that this arrangement means that each node typically has two peer objects and associated QUIC connections for every other node on the mesh - one client and one server.

The QUIC connection is arranged such that:
- Stream 0 is a control channel.
- Additional bidirectional streams are used one per [proxied](#proxying) object.
- Unidirectional streams are used to complete [proxied](#proxying) object transfers.

Peer QUIC streams are segmented into datagrams.
These datagrams are typed into several categories - Hello, Data, Control, and Signal.
The categories mean something different for the control channel and other channels.

#### Peer Control Channel

The control channel gets used for [non-proxying miscellany](/sdk/fidl/fuchsia.overnet.protocol/peer_protocol.fidl):
- Distribution of exported service lists via `update_node_description`.
- Initial connection to services and establishment of proxy channels via `connect_to_service`.
- Some proxy transfer management tasks.

When the control channel comes up, the first thing each side sends is a header containing FIDL flags and the current FIDL magic number.
This exchange sets parameters for all FIDL messages used by Overnet for the remainder of the connection, over all streams.
This header is not encapsulated in the normal datagram framing.

After the FIDL header, the framing protocol begins.
All messages on the control channel are of type Data, other types are disallowed.

The next message is a `ConfigRequest` (from the client), followed by a `ConfigResponse` from the server.
These are currently empty tables and are intended to provide a compatibility handshake for future Overnet protocol expansion.

The remainder of the messages exchanged are of type `PeerMessage` from the client, and `PeerReply` from the server.

#### Proxying

Peer objects are proxied over QUIC datagrams.
Control logic is implemented to adapt Zircon channels, sockets, and event pairs into peer QUIC streams.

To create a stream, first a QUIC stream ID is created.
Then, over a pre-existing stream, a message is sent that binds some object to that stream.
For Zircon handles, the message should contain a `ZirconHandle` object from the [zircon_proxy.fidl](/sdk/fidl/fuchsia.overnet.protocol/zircon_proxy.fidl) protocol.
Currently the two ways of doing this are via a `ZirconChannelMessage` on a channel based stream, or via a `ConnectToService` request on the control channel.

##### Data Frames

The kind of handle bound determines the usage of the Data datagram frame - a channel will transfer `ZirconChannelMessage` instances encoded via FIDL, a socket will exchange bytes.
Datagram oriented sockets will match Overnet Data frames 1:1 with datagrams from the socket.
Stream oriented sockets will ignore the framing and concatenate the bytes from the stream together.
Note that for channels the `ZirconChannelMessage` is encoded with FIDL options negotiated at peer connection time, whilst the payload it carries in the `bytes` field will contain a FIDL message encoded with potentially different options by the application layer.

##### Signal Frames

All proxyable Zircon objects, and consequently all currently supported Overnet stream types, support sending signals.
These signals are transported via the Signal datagram frame type, using `SignalUpdate` messages.

##### Control Transfer - Hello and Control frames

Proxy stream endpoints can be transferred between nodes.
The primary proxy stream is always bidirectional.
The first frame sent is a marker Hello frame sent from the initiator of the stream to the first endpoint.

Control frames transmit `StreamControl` messages, and are used to facilitate transfers.

1. When a node `A` wants to transfer control of a stream endpoint to another node `C`, it begins by sending a `begin_transfer` message.
This message captures the destination node, and a `transfer_key` used to label the transfer operation.
2. A 'drain stream' is also established as a unidirectional stream from `A` to `C`, and any messages received from the peer node `B` are forwarded on this stream (since there is a time period where `B` does not know that a transfer is in progress).
3. `B`, upon receipt of a `begin_transfer` message, the transfer is acknowledged with an `ack_transfer` reply.
The only further message allowed to be received is a required `shutdown` message with status `OK`.
If the transfer is to the receiving node, then the two handles (the two ends of the proxied stream) are "rejoined" and the transfer is completed by Overnet ceasing to proxy anything.
If the transfer is to another node, then an `open_transfer` request is sent on the peer control channel `B`->`C`, and normal proxying resumes. The `transfer_key` is sent along with this request so that the transfer can be located later.
4. `A`, upon receipt of the `ack_transfer` will finally construct a `StreamRef` indicating that it was a `transfer_initiator` and so sending the drain stream id, the new destination node, and the `transfer_key`.
With this information the receiving peer can tie together the transfer and reconstruct an ordered set of messages.

Note that at 3 in the algorithm below, `B` may have decided to also transfer control to `C` or a different node `D`.
In that case, each peer will receive a `begin_transfer` message where they expected to receive an `ack_transfer`.
We need to behave differently on each side to successfully complete this 4-way transfer, so we arbitrarily label the QUIC Client the `transfer_initiator` and the QUIC Server the `transfer_awaiter`.
(There's nothing important about Client/Server here, it could have equally be higher/lower numbered node id or any other deterministic decision that can be agreed upon by each peer).

`A` and `B` now immediately agree that the `transfer_awaiter` assigns the overall `transfer_key`, and the `transfer_initiator` `transfer_key` is dropped.
At this point the `StreamRef` can be formed and both sides can continue the transfer.

### Routing

It's expected that there are few nodes on a given Overnet mesh, which makes it practical to employ a total knowledge routing algorithm.

Each [Link](#links) `A` communicates with its peer node `B` the set of nodes that `B` can reach by sending a packet to `A`, along with some metrics for each route.
Note that this is the list of nodes that `A` can reach, absent the list of nodes that `A` would choose to reach via `B`.
Communication is via the link control protocol.
The mesh wide routing tables are eventually consistent, and route loops are guaranteed ephemeral.

Each [Node](#meshes-and-nodes) maintains a list of other nodes in the system, along with a preferred route to reach that node.
When this routing table changes, updates are sent to other links.

Each packet received by a link for a node other than the node receiving consults the routing table for the next step in the chain.

Routes are figured out to produce forwarding tables in [routes.rs](/src/connectivity/overnet/lib/core/src/router/routes.rs).
Communication of routing tables (both incoming and outgoing) is in the [Link code](/src/connectivity/overnet/lib/core/src/link/mod.rs).
Finally, local link RTTs are assembled and fed into the route planner in [link_status_updater.rs](/src/connectivity/overnet/lib/core/src/router/link_status_updater.rs).

## Embedding in different operating systems

### Fuchsia implementation

Overnet on Fuchsia is provided by the overnetstack component.
Applications can use Overnet by accessing the capabilities overnetstack exports.
It's expected that there be one overnetstack instance per device, or per user identity on a device, and that instance service many other Fuchsia components.

### Host implementation

Since non-Fuchsia operating systems do no provide Zircon channels or sockets, an emulation is provided for them.
That emulation is in-process only, and to provide inter-process exchange of these objects, we leverage Overnet.
As such, each binary on host embeds Overnet with a single link protocol to connect to ascendd.
Ascendd is a binary that provides a unix domain socket server for other binaries to connect to.
It also provides link protocols to reach Overnet instances off device.
Ascendd is again embeddable - ffx does this to co-host ascendd and the ffx daemon for instance.

## Protocol evolvability touchpoints

* Link protocols exchange an Introduction message that can be used for link protocol agreement.
* Peer protocols are always QUIC, so send an initial protocol string that can be used to fork the protocol.
* FIDL version negotiation is done as the first four bytes on a peer control stream.
* Higher level peer protocol decisions can be made via the ConfigRequest/Response messages.
* Peer stream framing has an 8-byte message type, of which only values 0, 1, 2, 3 are used.
There could be flag bits specced into this number.

## QUIC

QUIC is used in two places in the Overnet stack.

It's primarily and always used for communications between peers.
In the original (now deleted, C++) Overnet prototype, a custom packet based protocol was engineered.
At the time QUIC was bound tightly to HTTP semantics, which Overnet preferred not to share.
During prototyping a necessary feature set emerged: Overnet needed to transport large datagrams reliably and in order to provide proxying of Zircon objects.
As QUIC and HTTP/3 evolved, the QUIC standard shed many of the HTTP-isms that were inappropriate, and since the reliable byte streams it provided could easily be turned into datagram streams by adding framing, it had exactly the feature set that was needed for the peer protocol.
As a bonus, there was a well worked out TLS implementation in place, and third party libraries implementing the protocol, so that Overnet did not need to reinvent that either.

Later, when it was time to implement a secured UDP protocol, it was noticed that QUIC had added datagram support.
Since we already had working abstractions around QUIC, it became natural to leverage code we were already linking to implement the link protocol too.
