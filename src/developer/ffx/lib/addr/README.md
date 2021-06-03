# addr - Fuchsia Target Address Utilities

Rusts IP and Socket address types can leave a little gap, particularly that IPAddrV6 is not routable
for link-local addresses (lacks a scope), and SocketAddr demands a port.

This library provides a standard type for addresses, with conveniences for conversions to and from
other types.
