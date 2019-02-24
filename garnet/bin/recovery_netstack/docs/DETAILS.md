# Recovery Netstack Implementation Details

This document describes various implementation details that are confusing enough
to be worth calling out explicitly, and cannot naturally be documented in inline
code comments.

## Ethernet Frame Padding

Ethernet frames have a minimum payload length. In order to ensure that this
minimum is achieved, small payloads must sometimes be padded with zeroes. The
Ethernet header's EtherType field may sometimes be used to encode a length. If
this is the case, then the real length of the payload (not including the padding)
can be encoded in the EtherType, and so the receiver will know how many bytes of
the payload to pay attention to.

However, the EtherType field is sometimes used to indicate the type of the
encapsulated packet (e.g., IPv4, ARP, etc). In this case, it does not encode the
payload length. When this happens, the receiver must a) be able to parse a
payload whose length is longer than the length of the real encapsulated packet
and, b) be able to infer from the payload's header (e.g., the IPv4 header, the
ARP header, etc) how many bytes of the payload to parse, and how many to treat
as padding, and thus to ignore.

See for details:
- https://stackoverflow.com/a/35077070
- https://serverfault.com/a/510688
