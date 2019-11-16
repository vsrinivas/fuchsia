# Overnet - Fuchsia Overlay Network

Overnet is a suite of protocols, libraries and binaries that provide routed
and structured overlay networking to Fuchsia.

Overnet provides a mesh network that sits atop other protocols, such as
traditional networking stacks and provides message routing.

The Overnet implementation is intended to be portable - that is it can be
built on a wide variety of operating systems and provides cross-system
connectivity.

## Getting Started

Users wishing to establish a host-target Overnet network are best serviced
using the `fx overnet` tool. The process is as follows:

- Start `fx ascendd`. Ascendd provides the host "Overnet node".
- Start `fx overnet`. This creates a host-pipe connection between the host
  `ascendd` and the current `fx` target device.
- Run an Overnet program, for example: `fx onet list-peers`.

## Tools and Libraries

In the subtree, users will find the following items:

### Examples

There are two examples available:

- **echo** a simple echo server and client.
- **interface_passing** an example demonstrating sub-protocol routing (handle passing).

### Libraries

**core** is the central implementation of Overnet providing facilities for
programs operating either/both as server or client. The implementation
defines the protocols for routing, secure transport, service discovery and
many other features.

**hoist** provides a set of portability shims that when used enable a single
code base to be usable in both a Fuchsia environment, as well as a Unix style
environment (wherever the underlying IO reactor primitives operate). Hoist
abstracts over service connections.

### Overnetstack

Overnetstack is a Fuchsia component that hosts Overnet service routing on a
Fuchsia device. It also implements node and service discovery via mDNS.

### Tools

**ascendd** The ascend daemon is a *host* (Unix/POSIX) program that provides
Overnet node capabilities on a non-Fuchsia system. It binds to a Unix socket
that Overnet client programs can use to establish Overnet connections, and
both export and consume Overnet services.

**onet** The onet tool is a debugging tool for Overnet that performs a variety
*of functions, such as enumerating an Overnet network. It also provides a
*connection proxying facility that can be used in concert with **ascendd** to
*establish a connection between ascendd and a Fuchsia target device.
