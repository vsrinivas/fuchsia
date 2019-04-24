# Parsing and Serialization

This document describes the design of parsing and serialization in the netstack.

## Packet Buffers

The design of parsing and serialization is organized around the principle of
zero copy. Whenever a packet is parsed, the resulting packet object (such as
`wire::ipv4::Ipv4Packet`) is simply a reference to the buffer it was parsed
from, and so parsing involves no copying of memory. This allows for a number of
desirable design patterns.

*Most of the design patterns relating to buffers are enabled by utilities
provided by the `packet` crate. See its documentation
[here](https://fuchsia-docs.firebaseapp.com/rust/packet/index.html).*

### Packet Buffer Reuse

Since packet objects are merely views into an existing buffer, when they are
dropped, the original buffer can be modified directly again. This allows buffers
to be reused once the data inside of them is no longer needed. To accomplish
this, we use the `packet` crate's `ParseBuffer` trait, which is a buffer that
can keep track of which bytes have already been consumed by parsing, and which
are yet to be parsed. The bytes which have not yet been parsed are called the
"body", and the bytes preceding and following the body are called the "prefix"
and the "suffix."

In order to demonstrate how we use buffers in the netstack, consider this
hypothetical control flow in response to receiving an Ethernet frame:

1. The Ethernet layer receives a buffer containing an Ethernet frame. It parses
   the buffer as an Ethernet frame. The EtherType indicates that the
   encapsulated payload is an IPv4 packet, so the payload is delivered to the IP
   layer for further processing. Note that the *entire* original buffer is
   delivered to the IP layer. However, since the Ethernet header has already
   been parsed from the buffer, the buffer's body contains only the bytes of the
   IP packet itself.
2. The IP layer parses the payload as an IPv4 packet. The IP protocol number
   indicates that the encapsulated payload is a TCP segment, so the payload is
   delivered to the TCP layer for further processing. Again, the entire original
   buffer is delivered to the TCP layer, with the body now equal to the bytes of
   the TCP segment.
3. The TCP layer parses the payload as a TCP segment. It contains data which the
   TCP layer would like to acknowledge. Once the TCP layer has extracted all of
   the data that it needs out of the segment, it drops the segment (the
   `TcpSegment` object, which itself borrowed the buffer), regaining direct
   mutable access to the original buffer. Since the buffer has access to the
   now-parsed prefix and suffix in addition to the body, the TCP stack can now
   use the entire buffer for serializing. It figures out how much space must be
   left at the beginning of the buffer for all lower-layer headers (in this
   case, IPv4 and Ethernet), and serializes a TCP Ack segment at the appropriate
   offset into the buffer. Now that the buffer is being used for serialization
   (rather than parsing), the body indicates the range of bytes which have been
   serialized so far, and the prefix and suffix represent empty space which can
   be used by lower layers to serialize their headers and footers.
4. The TCP layer passes the buffer to the IP layer, with the body equal to the
   bytes of the TCP segment that has just been serialized. The IP layer treats
   this as the body for its IP packet. It serializes the appropriate IP header
   into the buffer's prefix, just preceding the body. It expands the body to
   include the now-serialized header, leaving the body equal to the bytes of the
   entire IP packet.
5. The IP layer passes the buffer to the Ethernet layer, with the body now
   corresponding to the IP packet that it has just serialized. The Ethernet
   layer treats this as the body for its Ethernet frame. It serializes the
   appropriate Ethernet header, expands the body to include the bytes of the
   entire Ethernet frame, and passes the buffer to the Ethernet driver to be
   written to the appropriate network device.

Note that, in this entire control flow, only a single buffer is ever used,
although it is at times used for different purposes. If, in step 3, the TCP
layer finds that the buffer is too small for the TCP segment that it wishes to
serialize, it can still allocate a larger buffer. However, so long as the
existing buffer is sufficient, it may be reused.

### Prefix, Suffix, and Padding

When using a single buffer to serialize a packet - including any encapsulating
headers of lower layers of the stack - it is important to satisfy some
constraints:
- There must be enough space preceding and following an upper-layer body for
  lower-layer headers and footers.
- If any lower-layer protocols have minimum body length requirements, there must
  be enough space following an upper-layer body for any padding bytes needed to
  satisfy those requirements.

Consider, for example, Ethernet. When serializing an IPv4 packet inside of an
Ethernet frame, the IPv4 packet must leave enough room for the Ethernet header.
Ethernet has a minimum body requirement, and so there must also be enough bytes
following the IPv4 packet to be used as padding in order to meet this minimum in
case the IPv4 packet itself is not sufficiently large.

To accomplish this, we use the `packet` crate's `Serializer` trait. A
`Serializer` represents the metadata needed to serialize a request in the
future. `Serializer`s may be nested, which results in a `Serializer` describing
a sequence of encapsulated packets to be serialized, each being used as the body
of an encapsulating packet. When a sequence of nested `Serializer`s is
processed, the header, footer, and minimum body size requirements are computed
starting with the outermost packet and working in. Once the innermost
`Serializer` is reached, it is that `Serializer`'s responsibility to provide a
buffer:
- The buffer must contain the body to be encapsulated in the next layer. The
  buffer implements the `BufferMut` trait (a superset of the functionality
  required by the `ParseBuffer` trait mentioned above), and the buffer's body
  indicates the bytes to be encapsulated by the next layer.
- The buffer must satisfy the header, footer, and minimum body size requirements
  by providing enough prefix and suffix bytes before and after the body.

Once the innermost `Serializer` has produced its buffer, each subsequent packet
serializes its headers and footers, expands the buffer's body to contain the
whole packet as the body to be encapsulated in the next layer, and returns the
buffer to be handled by the next layer.

When control makes its way to a layer of the stack that has a minimum body
length requirement, that layer is responsible for consuming any bytes following
the range for use as padding (and zeroing those bytes for security). This logic
is handled by `EncapsulatingSerializer`'s implementation of `serialize`.

`Serializer` is implemented by a number of different types, allowing for a range
of serialization scenarios including:
- Serializing a new packet in a buffer which previously stored an incoming
  packet.
- Forwarding a packet by shrinking the incoming buffer's range to the body of
  the packet to be serialized, and then passing that buffer back down the stack
  of `Serializer`s.

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
bytes beyond the end of the body. In the `BufferMut` used to store packets
during serialization, these padding bytes are in the "suffix" just following the
buffer's body.

The logic for adding padding is provided by `EncapsulatingSerializer`, described
in the *Prefix, Suffix, and Padding* section above. `EncapsulatingSerializer`
ensures that these padding bytes are zeroed.

*See also: the `_zeroed` constructors of the `zerocopy::LayoutVerified` type*
