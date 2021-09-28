<!-- mdformat off(templates not supported) -->
{% set rfcid = "RFC-0109" %}
{% include "docs/contribute/governance/rfcs/_common/_rfc_header.md" %}
# {{ rfc.name }}: {{ rfc.title }}
<!-- SET the `rfcid` VAR ABOVE. DO NOT EDIT ANYTHING ELSE ABOVE THIS LINE. -->

<!-- mdformat on -->

<!-- This should begin with an H2 element (for example, ## Summary).-->

## Summary

Implement network datagram socket data transport using zircon sockets.
Implement client-side argument validation using client-cached network state.

## Motivation

Increase datagram socket throughput and reduce CPU utilization.

Prior to <https://fxbug.dev/21123> datagram network sockets were implemented
using zircon sockets; clients would `zx_socket_write` to send data and
`zx_socket_read` to receive data. A minimal protocol was used to carry metadata
such as the destination address, if one was provided by the calling
application.

This approach was abandoned because it did not provide for error signaling to
the caller in certain cases. Consider an application that wishes to send a
payload to a remote to which the network stack does not have a route; in order
to comply with the expectations of third party software targeting Linux, the
implementation must return an error to indicate that no action was taken on
behalf of the caller.

The current implementation is defined in
[`fuchsia.posix.socket/DatagramSocket.{Recv,Send}Msg`][fsocket].
Both sending and receiving data requires multiple context switches due to
support FIDL responses. Ignoring FIDL serialization:

```
+-----------------------------+-------------------------+
| Client                      | Netstack                |
+-----------------------------+-------------------------+
| `zx_channel_call(...)`      |                         |
|                             |                         |
|                             | `zx_channel_read(...)`  |
|                             | `zx_channel_write(...)` |
|                             |                         |
| (`zx_channel_call` returns) |                         |
+-----------------------------+-------------------------+
```

The reverse (receiving data) looks much the same. Compare this to
unacknowledged I/O:

```
+-------------------------+------------------------+
| Client                  | Netstack               |
+-------------------------+------------------------+
| `zx_channel_write(...)` |                        |
+-------------------------+------------------------+
| `zx_channel_write(...)` | `zx_channel_read(...)` |
+-------------------------+------------------------+
```

While unacknowledged I/O is possible with zircon channels and FIDL, it does not
provide backpressure, and can lead to runaway memory growth. Thus, we propose
to use zircon *sockets*:

```
+------------------------+-----------------------+
| Client                 | Netstack              |
+------------------------+-----------------------+
| `zx_socket_write(...)` |                       |
+------------------------+-----------------------+
| `zx_socket_write(...)` | `zx_socket_read(...)` |
+------------------------+-----------------------+
```

## Design

Utilizing unacknowledged I/O presupposes that datagram socket I/O can be
entirely validated on the local machine without interaction with the network,
and that the results of this validation can be cached across multiple
interactions with the socket (so that the validation cost can be amortized).

Note that this assumption notably does not hold for `IPPROTO_ICMP` sockets -
their payloads are checked for validity - so the existing FIDL-based protocol
will be retained and used where performance is not critical and deep
validation is required.

Extract FIDL from existing types to be reused and rename `DatagramSocket` for
clarity:

```fidl
protocol BaseDatagramSocket {
  compose BaseSocket;

  /// Retrieves creation information from the socket.
  GetInfo() -> (Domain domain, DatagramSocketProtocol proto) error fuchsia.posix.Errno;
}

protocol SynchronousDatagramSocket {
  compose BaseDatagramSocket;

  /// Receives a message from the socket.
  RecvMsg(bool want_addr, uint32 data_len, bool want_control, RecvMsgFlags flags) -> (fuchsia.net.SocketAddress? addr, bytes data, RecvControlData control, uint32 truncated) error fuchsia.posix.Errno;
  /// Sends a message on the socket.
  SendMsg(fuchsia.net.SocketAddress? addr, bytes:MAX data, SendControlData control, SendMsgFlags flags) -> (int64 len) error fuchsia.posix.Errno;
}
```

Define the new FIDL protocol with validation functions:

```fidl
/// Matches the definition in //zircon/system/public/zircon/types.h.
const uint32 ZX_WAIT_MANY_MAX_ITEMS = 64;

/// Describes an intent to send data.
table SendMsgArguments {
  /// The destination address.
  ///
  /// If absent, interpreted as the method receiver's connected address and
  /// causes the connected address to be returned in [`SendMsgBoardingPass.to`].
  ///
  /// Required if the method receiver is not connected.
  1: fuchsia.net.SocketAddress to;
}

/// Describes a granted approval to send data.
resource table SendMsgBoardingPass {
  /// The validated destination address.
  ///
  /// Present only in response to an unset
  /// [`SendMsgArguments.to`].
  1: fuchsia.net.SocketAddress to;
  /// Represents the validity of this structure.
  ///
  /// The structure is invalid if any of the elements' peer is closed.
  /// Datagrams sent to the associated destination after invalidation will be
  /// silently dropped.
  2: vector<zx.handle:<EVENTPAIR, zx.RIGHTS_BASIC>>:ZX_WAIT_MANY_MAX_ITEMS validity;
  /// The maximum datagram size that can be sent.
  ///
  /// Datagrams exceeding this will be silently dropped.
  3: uint32 maximum_size;
}

protocol DatagramSocket {
  compose BaseDatagramSocket;

  /// Validates that data can be sent.
  ///
  /// + request `args` the requested disposition of data to be sent.
  /// - response `pass` the constraints sent data must satisfy.
  /// * error the error code indicating the reason for validation failure.
  SendMsgPreflight(SendMsgArguments args) -> (SendMsgBoardingPass pass) error fuchsia.posix.Errno;
};
```

Define FIDL structures to be sent on the zircon socket:

```fidl
/// A datagram received on a network socket, along with its metadata.
table RecvMsgPayload {
  1: fuchsia.net.SocketAddress from;
  2: bytes:MAX datagram;
  3: RecvControlData control;
};

/// A datagram to be sent on a network socket, along with its metadata.
table SendMsgPayload {
  1: SendMsgArguments args;
  2: bytes:MAX datagram;
  3: SendControlData control;
};
```

These structures would be encoded and decoded using FIDL-at-rest, which is not
yet fully specified (see <https://fxbug.dev/45252>).

Note that this representation does not eliminate the extra data copy incurred
in the FIDL deserialization path even in the presence of the vectorized socket
I/O operations proposed in <https://fxrev.dev/526346>. This overhead is not
addressed in this proposal, and may be the focus of future work, should the
need arise.

Clients wanting to send data obey the following (horizontally squished) diagram:

```
+--------------------------------+               +---------------------------+               +----------------+
| cache.getSendMsgBoardingPass() | - Present ->  | checkPeerClosed(validity) |   +- ZX_OK -> | Return success |
+--------------------------------+               +---------------------------+   |           +----------------+
 |    ^                                            ^                     |  |    |
 |    |                                            |                     |  |  +------------------------------+
 |  +------+                  +----------------------------------+       |  |  | socket.write(SendMsgPayload) | - != ZX_OK -----+
 |  | Send |    +- Success -> | cache.storeSendMsgBoardingPass() |       |  |  +------------------------------+                 |
 |  +------+    |             +----------------------------------+       |  |                         ^                         |
 |            +--------------------+                                     |  |                         |                         |
 +- Absent -> | SendMsgPreflight() |  +- (ZX_OK, ZX_SIGNAL_PEER_CLOSED) -+  +- (ZX_ERR_TIMED_OUT) -+  |                         |
              +--------------------+  |                                                            |  +- No -+                  |
                |                ^    |   +-----------------------------------+                    |         |                  |
                |                |    +-> | cache.removeSendMsgBoardingPass() |                    |   +---------------------+  |
                |                |        +-----------------------------------+                    +-> | size > maximum_size |  |
                |                |          |                                                          +---------------------+  |
                |                |          |  +--------------+                                              |                  |
                |                +----------+  |              | <-------------------------------------- Yes -+                  |
                |                              | Return error |                                                                 |
                +- Failure ------------------> |              | <---------------------------------------------------------------+
                                               +--------------+
```

Where the client's `cache` "implements" `SendMsgPreflight`; it is roughly a map
from `fuchsia.net.SocketAddress` to `(vector<zx::eventpair>, maximum_size)`.

Note that the cache formed by this strategy is eventually-consistent; it is
possible for invalidation to interleave a client's validity check and its
payload arriving at the network stack. This is acceptable for datagram sockets
whose delivery semantics are best-effort.

## Implementation

Add the new implementation to [`fuchsia.io/NodeInfo`][fuchsia-io-NodeInfo]:

```fidl
resource union NodeInfo {
    /// The connection composes [`fuchsia.posix.socket/DatagramSocket`].
    N: DatagramSocket datagram_socket;
};

/// A [`NodeInfo`] variant.
// TODO(https://fxbug.dev/74683): replace with an anonymous struct inline.
resource struct DatagramSocket {
    zx.handle:<SOCKET, zx.RIGHTS_BASIC | zx.RIGHTS_IO> socket;
};
```

Change the return type of
[`fuchsia.posix.socket/Provider.DatagramSocket`][fsocket-provider-dgram-sock]
to a variant:

```fidl
/// Contains a datagram socket implementation.
resource union DatagramSocketImpl {
    1: DatagramSocket datagram_socket;
    2: SynchronousDatagramSocket synchronous_datagram_socket;
}
```

...and change the behavior such that a `DatagramSocket` is returned whenever
permitted by the arguments (i.e. the caller did not request an ICMP socket).

The initial implementation is expected to supply two elements in each
`SendMsgBoardingPass.validity`:

1. Represents a known state of the routing table; shared by all sockets and
   invalidated on any change to the routing table.
2. Represents a known state of the particular socket; invalidated on any change
   to the socket that may change the socket's behavior e.g. calls to `bind`,
   `connect`, `setsockopt(..., SO_BROADCAST, ...)`,
   `setsockopt(..., SO_BINDTODEVICE, ...)`, etc.

## Performance

Throughput of `SOCK_DGRAM` sockets is expected to approximately double; this
estimate is based on the performance regression seen after
<https://fxbug.dev/21123>.

CPU utilization is expected to decrease by a meaningful but unknown magnitude.

## Ergonomics

This change does not have meaningful impact on ergonomics as downstream users do
not directly consume the interfaces presented here.

## Backwards Compatibility

Preserve ABI compatibility by initially leaving
[`fuchsia.posix.socket/Provider.DatagramSocket`][fsocket-provider-dgram-sock]
unchanged and implementing the new functionality as `DatagramSocket2`.
Following necessary ABI transition, rename `DatagramSocket2` to
`DatagramSocket` and remove the previous implementation. Following another ABI
transition, remove `DatagramSocket2`.

## Security considerations

This proposal has no impact on security.

## Privacy considerations

This proposal has no impact on privacy.

## Testing

Existing unit tests cover the functionality affected.

## Documentation

No documentation is necessary apart from FIDL doc comments presented here.

## Drawbacks, alternatives, and unknowns

This proposal addresses the motivation by building machinery in userspace.
Another possibility is to build this machinery in the kernel.

A sketch for translating it to kernel:

1. With each `zx::socket` endpoint, the kernel would maintain a map from
   `SocketAdddres` to `max_size`.
1. We'd add some `zx_socket_add_route` / `zx_socket_remove_route` system calls
   for modifying that map on the peer endpoint.
1. We'd add some `zx_socket_send_to` / `zx_socket_receive_from` system calls
   that would consume/provide addresses.

If userspace called `zx_socket_send_to` with an address that wasn't in the map,
the operation would fail and userspace would need to send a synchronous message
to the netstack to request that route be added to the `zx::socket`. If that
request failed, then the address operation fails with an error.

### Pros

In the kernel approach, sending a UDP packet (in the fast case) is a single
syscall (`zx_socket_send_to`) rather than two syscalls (`zx_object_wait_many`,
`zx_socket_write`).

This is potentially a non-pro because of possible optimizations to the
userspace approach. Realizing that we always `zx_object_wait_many` with
`time::infinite_past`, we could optimized the operation to do its work without
a system call, provided that the necessary state is maintained using atomic
operations. This might require the handle table to be in the vDSO as well,
which may not be the case and may not be possible.

An alternative for clients with runtimes is to use `zx_object_wait_async`
instead of `wait_many` to maintain the local cache, allowing the fast path to
avoid the extra syscall.

We also avoid the dependency on FIDL-at-rest *and* the extra data copy inherent
in FIDL because the message payload is baked into the system calls, which can
copy the payload directly to the final destination.

### Cons

In the kernel approach, there isn't an obvious way to do O(1) route
cancellation when the routing table changes. As described, we could add a flag
to `zx_socket_remove_route` that removes all the routes (probably desirable
anyway), but the netstack would need to issue a `zx_socket_remove_route` on
every socket.

We could get super fancy and have `zx_socket_add_route` take an eventpair for
cancellation, but that's getting pretty baroque.

Baking these structures into the kernel is also costly in terms of introducing
yet another protocol evolution model to the platform; we'd now have a much
tighter coupling between particular FIDL types and system calls, which would
not automatically remain in sync.

### Unknowns

There's also a question about how to deal with `SendControlData`. Perhaps that
would need to be an additional parameter to `zx_socket_send_to` or maybe a flag
on the operation.

[fsocket]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.posix.socket/socket.fidl;l=292-295;drc=0661adfd75b2c6c49b7cdb2c4edba7507c1e12ea
[fsocket-provider-dgram-sock]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.posix.socket/socket.fidl;l=575;drc=0661adfd75b2c6c49b7cdb2c4edba7507c1e12ea
[fuchsia-io-NodeInfo]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=15;drc=0661adfd75b2c6c49b7cdb2c4edba7507c1e12ea
