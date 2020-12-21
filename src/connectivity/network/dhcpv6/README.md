# Fuchsia DHCPv6 Client

## Overview

Fuchsia's DHCPv6 client implements the client side of the DHCPv6 protocol
defined in [RFC 8415](https://tools.ietf.org/html/rfc8415).

Implementation is split into [core and bindings](#core-and-bindings). The purely
functional core implements the DHCPv6 client state machine, and the imperative
bindings interact with other systems on Fuchsia through their corresponding APIs
to fulfill actions necessary for the client to be useful, e.g. send replies
through a socket, send updates to watchers, etc.

The DHCPv6 client does **not** modify any configurations on the host system
directly. Instead, when it learns new configurations, it sends updates through
[watcher APIs](#watcher-apis).

## Parsing and Serialization

[packet_formats_dhcp::v6](https://fuchsia-docs.firebaseapp.com/rust/packet_formats_dhcp/v6/index.html)
implements parsing and serialization of
[DHCPv6 messages](https://tools.ietf.org/html/rfc8415#section-8). The
implementation is based on the
[packet crate](https://fuchsia-docs.firebaseapp.com/rust/packet/index.html). The
packet crate minimizes copying during parsing and serialization. It also allows
easy nesting of DHCPv6 messages inside UDP messages, which is especially useful
when building test packets.

## Core and Bindings

The core and bindings split is inspired by the design of netstack3 on Fuchsia.
This design splits the implementation into a purely functional core and an
imperative shell (bindings). Because the core is purely functional, it is
naturally cross-platform. The bindings, on the other hand, usually rely on
platform-specific APIs to carry out imperative tasks. Netstack3 uses this design
to implement all protocols it supports. More details about this design can be
found in
[netstack3's "Core and Bindings" doc](../netstack3/docs/CORE_BINDINGS.md).

For the DHCPv6 client, the core:

*   Implements a purely functional state machine that emits
    [Actions](https://fuchsia-docs.firebaseapp.com/rust/dhcpv6_core/client/enum.Action.html).
    This state machine expects someone to fulfill the actions (e.g. send
    messages, scheduler timers, etc.) and dispatch events (e.g. message
    received, timer fired, etc.) for it to transition states.

*   Uses `packet_formats_dhcp::v6` to parse and serialize DHCPv6 messages.

The imperative shell:

*   Binds to a socket that talks to DHCPv6 servers on the network. It passes all
    received messages to the core and let it determine what to do. It then
    fulfills the actions returned by the core, e.g. sends a reply to the server,
    or replies to a watcher about configuration changes.

*   Runs a FIDL server that implements APIs defined in [fuchsia.net.dhcpv6][1].
    The APIs include [fuchsia.net.dhcpv6.ClientProvider][1] for launching new
    clients, and [fuchsia.net.dhcpv6.Client][1] for watching updates from
    running clients.

## Watcher APIs

Watcher APIs use the
[hanging get pattern](../../../../docs/concepts/api/fidl.md#hanging-get) to
allow watchers to pull changes from servers. Pulling is preferred over pushing
(i.e. the server pushes data to the client) because pull models have built-in
flow control since the client naturally limits the rate at which the server
produces data. Hanging get is the recommended pattern widely used to implement
pull-based protocols on Fuchsia.

The DHCPv6 client exposes a set of watcher APIs instead of applying the
configurations directly (DHCPv6 is acting as the "server" from the hanging get
pattern).

## Tests

*   Parsing and serialization tests:
    [packet_formats_lib_test.cmx](../../../lib/network/packet-formats/meta/packet_formats_lib_test.cmx).

*   Core unit tests: [dhcpv6-core-test.cmx](core/meta/dhcpv6-core-test.cmx).

*   Client bindings unit tests:
    [dhcpv6-client-test.cmx](client/meta/dhcpv6-client-test.cmx).

*   Integration test:
    [dhcp-validity-test.cmx](../tests/dhcp_interop/meta/dhcp_validity_test.cmx).
    This test uses netemul to launch a DHCPv6 client on Fuchsia, and a DHCPv6
    server on Debian in the same network, to make sure the client can talk to a
    DHCPv6 server in the same nework.

[1]: ../../../../sdk/fidl/fuchsia.net.dhcpv6/client.fidl
