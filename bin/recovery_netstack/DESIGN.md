# Recovery Netstack Design

This document describes various high-level design concepts employed in
`recovery_netstack`.

## Packet Buffers

The design of parsing and serialization is organized around the principle of
zero copy. Whenever a packet is parsed, the resulting packet object (such as
`wire::ipv4::Ipv4Packet`) is simply a reference to the buffer it was parsed
from, and so parsing involves no copying of memory. This allows for a number of
desirable design patterns.

### Packet Buffer Reuse

Since packet objects are merely views into an existing buffer, when they are
dropped, the original buffer can be modified directly again. This allows buffers
to be reused once the data inside of them is no longer needed. For example,
consider this hypothetical control flow in response to receiving an Ethernet
frame:

1. The Ethernet layer receives a buffer containing an Ethernet frame. It parses
   the buffer as an Ethernet frame. The EtherType indicates that the
   encapsulated payload is an IPv4 packet, so the payload is delivered to the IP
   layer for further processing. Note that the *entire* original buffer is
   delivered to the IP layer along with the range of bytes which corresponds to
   the IP packet itself.
2. The IP layer parses the payload as an IPv4 packet. The IP protocol number
   indicates that the encapsulated payload is a TCP segment, so the payload is
   delivered to the TCP layer for further processing. Again, the entire original
   buffer is delivered to the TCP layer along with the range of bytes
   corresponding to the TCP segment itself.
3. The TCP layer parses the payload as a TCP segment. It contains data which the
   TCP layer would like to acknowledge. Once the TCP layer has extracted all of
   the data that it needs out of the segment, it drops the segment, regaining
   direct access to the original buffer. Since the entire buffer - not just the
   sub-set of the buffer containing the payload - has been passed up the stack,
   the TCP stack still has access to the entire buffer. It figures out how much
   space must be left at the beginning of the buffer for all lower-layer headers
   (in this case, IPv4 and Ethernet), and serializes a TCP Ack segment at the
   appropriate offset into the buffer.
4. The TCP layer passes the buffer to the IP layer, indicating the range within
   the buffer corresponding to the TCP segment that it has just serialized. The
   IP layer treats this range as the body for its IP packet. It serializes the
   appropriate IP header just preceding the already-written body.
5. The IP layer passes the buffer to the Ethernet layer, indicating the range
   within the buffer corresponding to the IP packet that it has just serialized.
   The Ethernet layer treats this range as the body for its Ethernet frame. It
   serializes the appropriate Ethernet header, and passes the layer to the
   Ethernet driver to be written to the appropriate network device.

Note that, in this entire control flow, only a single buffer is ever used,
although it is at times used for different purposes. If, in step 3, the TCP
layer finds that the buffer is too small for the TCP segment that it wishes to
serialize, it can still allocate a larger buffer. However, so long as the
existing buffer is sufficient, it may be reused.

### Prefix and Padding

When using a single buffer to serialize a packet - including any encapsulating
headers of lower layers of the stack - it is important to satisfy some
constraints:
- There must be enough space preceding an upper-layer body for lower-layer
  headers.
- If any lower-layer protocols have minimum body length requirements, there must
  be enough space following an upper-layer body for any padding bytes needed to
  satisfy those requirements.

Consider, for example, Ethernet. When serializing an IPv4 packet inside of an
Ethernet frame, the IPv4 packet must leave enough room for the Ethernet header.
Ethernet has a minimum body requirement, and so there must also be enough bytes
following the IPv4 packet to be used as padding in order to meet this minimum in
case the IPv4 packet itself is not sufficiently large.

In `recovery_netstack`, the canonical way to convey this information is through
a `BufferAndRange`. A `BufferAndRange` represents a buffer which can be used for
parsing or serialization and a range into that buffer. When serializing an
upper-layer packet, the caller must first:
- Figure out how many bytes of prefix to leave before the upper-layer packet in
  order to leave room for headers
- Figure out what the minimum body length requirement will be, and make sure to
  leave enough bytes after the upper-layer packet so that the upper-layer packet
  and padding bytes combined will satisfy this requirement

Then, the caller constructs a `BufferAndRange` with the appropraite prefix and
suffix bytes, and whose range is equal to the upper-layer packet, and passes
this range to the layer below it. The layer below treats the range as its body,
and serializes any headers in the bytes preceding the range. When control
finally makes its way to a layer of the stack that has a minimimum body length
requirement, that layer is responsible for consuming any bytes following the
range for use as padding.

If an existing buffer is to be re-used, the `ensure_prefix_padding` function can
be used to ensure that the buffer has enough prefix and suffix bytes to satisfy
the requirements discussed here, reallocating a larger buffer if necessary.

*See also: `wire::SerializationCallback`*

### In-Place Packet Modification

In certain scenarios, it is necessary to modify an existing packet before
re-serializing it. The canonical example of this is IP forwarding. When all
packets are simply references into a pre-existing buffer, modifying and then
re-serializing packets is cheap, as it can all be done in-place in the common
case. For example, if an IP packet is received which needs to be forwarded, so
long as the link-layer headers of the device over which it is to be forwarded
are not larger than the link-layer headers of the device over which it was
received, there will be enough space in the existing buffer to serialize new
link-layer headers and deliver the resulting link-layer frame to the appropriate
device without ever having to allocate extra buffers or copy any data between
buffers.

### Zeroing Buffers

If buffers that previously held other packets are re-used for serializing
new packets, then there is a risk that data from the old packets will leak
into the new packet if every byte of the new packet is not explicitly
initialized. In order to prevent this:
- Any code constructing the body of a packet is responsible for ensuring that
  all of the bytes of the body have been initialized.
- Any code constructing the headers of a packet (usually serialization code in a
  submodule of the `wire` module) is responsible for ensuring that all of the
  bytes of the header have been initialized.

A special case of these requirements is post-body padding. For packet formats
with minimum body size requirements, upper layers will provide extra buffer
bytes beyond the end of the body. These bytes are outside of the range used by
the `BufferAndRange` to indicate the body to be encapsulated, but are allocated.
The code constructing the packet with the minimum body size requirement will
extend the range to include the padding bytes just before passing the buffer to
the seriliazation function for the packet format in question. It is the
responsibility of such code to ensure that these padding bytes are zeroed before
calling the serialization function. *For more on padding and minimum body sizes,
see the "Prefix and Padding" section.*

*See also: The `_zeroed` constructors of the `zerocopy::LayoutVerified` type*

## IP Types

In order to fully leverage Rust's type system, we use two traits - `Ip` and
`IpAddr` - to abstract over the versions of the IP protocol. The `Ip` trait
represents the protocol version itself, including details such as the protocol
version number, the protocol's loopback subnet, etc. The `IpAddr` trait
represents an actual IP address - `Ipv4Addr` or `Ipv6Addr`.

As much as possible, code which must be aware of IP version should be generic
over either the `Ip` or `IpAddr` traits. This allows common code to be only
written once, while still allowing protocol-specific logic when needed. It also
leverages the type system to provide a level of compile-time assurance that
would not be possible using runtime types like an enum with one variant for each
address type. Consider, for example, this function from the `device` module:

```
/// Get the IP address associated with this device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device: DeviceAddr) -> Option<A>
```

Without trait bounds, this would either need to take an object at runtime
identifying which IP version was desired, which would lose type safety, or would
require two distinct functions, `get_ipv4_addr` and `get_ipv6_addr`, which would
result in a large amount of code duplication (if this pattern were used
throughout the codebase).

### Specialization

Sometimes, it is necessary to execute IP version-specific logic. In these cases,
it is necessary to have Rust run different code depending on the concrete type
that a function is instantiated with. We could imagine code along the lines of:

```
/// Get the IPv4 address associated with this Ethernet device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64) -> Option<A>;

pub fn get_ip_addr<Ipv4Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv4Addr> {
    get_device_state(state, device_id).ipv4_addr
}

pub fn get_ip_addr<Ipv6Addr>(state: &mut StackState, device_id: u64) -> Option<Ipv6Addr> {
    get_device_state(state, device_id).ipv6_addr
}
```

This is a feature called "bare function specialization", and it unfortunately
doesn't yet exist in Rust. However, a function called "impl specialization" does
exist, and we can leverage it to accomplish something similar. While the details
of implementing this logic using impl specialization involve some annoying
boilerplate, we provide the `specialize_ip!` and `specialize_ip_addr!` macros to
make it easier. Using `specialize_ip_addr!`, the above hypothetical code can be
written today as:

```
/// Get the IPv4 address associated with this Ethernet device.
pub fn get_ip_addr<A: IpAddr>(state: &mut StackState, device_id: u64) -> Option<A> {
    specialize_ip_addr!(
        fn get_ip_addr(state: &EthernetDeviceState) -> Option<Self> {
            Ipv4Addr => { state.ipv4_addr }
            Ipv6Addr => { state.ipv6_addr }
        }
    );
    A::get_ip_addr(get_device_state(state, device_id))
}
```

`get_ip_addr` is added as an associated function on the `IpAddr` trait, where
the implementation for `Ipv4Addr` is given in the first block, and the
implementation for `Ipv6Addr` is given in the second block. This allows us to
invoke it as `A::get_ip_addr`.

A few oddities to note: For reasons having to do with limitations of Rust
macros, the type which implements `IpAddr` must be written as `Self` in the
function definition. That's where the `Option<Self>` comes from. The body is
structured like a sort of type-level macro, where the branches are labeled by
the concrete types - `Ipv4Addr` or `Ipv6Addr` - that the function is
instantiated with. Finally, the blocks associated with these labels become the
function bodies, and so must return, in this case, `Option<Ipv4Addr>` or
`Option<Ipv6Addr>` respectively.