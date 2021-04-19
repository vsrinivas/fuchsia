# Overnet Problem Space

## Overview

Overnet is a networking technology intended to allow components in the same security domain communicate with each other without regard to underlying connectivity technologies.

This document serves to describe problems which Overnet tries to solve, and also discusses problems considered to be outside of Overnet's scope.

Note: Not everything is implemented yet.

## What does Overnet solve?

### Peer discovery

An Overnet mesh can automatically find its members and connects to them.

Consumers of Overnet can discover those peers and the capabilities they export.

### Routing

Since Overnet can communicate over more than one network technology, it provides packet routing between nodes.
These Overnet packets are encapsulated into whichever underlying network packets (or streams) are available.
Nodes on the mesh work together to calculate optimal routes between nodes.

### Connection Handoff

Overnet expects to be used in ambient computing environments where devices may come and go.
To facilatate this, Overnet communication endpoints can be reassigned to other nodes at any time.
This also allows, for example, a battery operated device to initiate a link separately to two wired devices, and then remove itself from the communications stream.

### Mesh Identity

An Overview mesh has an identity.
Each node has certificates that allow that node to join the mesh and be assigned a routable address.

### Public Nodes

Overnet will provide a mechanism for devices to join a variety of meshes and serve a suite of capabilities (via [Service discovery](#service-discovery)) to all of them.
Such public nodes will not allow traffic to be forwarded between meshes.

### Portability

Overnet is intended to run on most operating systems for bigger devices (beyond microcontrollers).
It is not restricted to Fuchsia devices only.

## What does Overnet not solve?

### Service discovery

Whilst Overnet nodes can list a set of capabilities, this is not intended to be the discovery mechanism most applications use.

Instead, the intent is that discovery systems should express a single capability via Overnet, and then use that capability to perform some richer discovery.
Such richer discovery may include additional authentication and rights management, or resource throttling, or service hierarchy.

### Naming

Overnet does not provide a stable identifier for nodes.
It is expected that node identifiers will change with some cadence.
At the time of writing that cadence is everytime Overnet is started - this is expected to change as authentication mechanisms are built out.

For systems requiring stable node identifiers, it's suggested a capability be exported via Overnet to supply that name.
Such a capability could be discovered via the [ListPeers](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.overnet/overnet.fidl#20) API and interrogated to discover the stable name.
Directory services could easily cache such information if desired.

### Application Protocol

Overnet tries to not provide an opinion on which protocol applications should communicate with, but instead provide primitives so that any protocol may be used.

FIDL via Zircon-like channels are used as a system protocol, and as the expected protocol for capabilities to be exchanged via the top level Overnet capability system.
However, once these capabilities are connected, it's a simple matter for a stream or datagram oriented socket to be exchanged over which to communicate with some other protocol.

It is expected that Overnet will build additional primitives to support important protocols as well if necessary.

### Transparent Proxying

Overnet does not expect to be a transparent proxy for any protocol.
Indeed, it's considered important that there be at least one point in code where going off-device is considered and allowed.
Typically right now that point is at channel construction time for capability connection.
Future service discovery systems wrapped around Overnet are encouraged to have some difficult-to-ignore designator that says a service connection may not be to the same device.

Overnet's proxying of Zircon types is intended to be limited to what is useful for messaging between applications.
For instance, it's not expected that processes or threads or virtual memory be exchangable via Overnet.
Nor is it expected that a KOID remain stable after transferring a handle to another device.
