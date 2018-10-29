// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ICMPv6

use std::ops::Range;

use zerocopy::ByteSlice;

use crate::error::ParseError;
use crate::ip::{Ipv6, Ipv6Addr};

use super::common::{IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpTimeExceeded};
use super::{peek_message_type, HasOriginalPacket, IcmpIpExt, IcmpPacket, IcmpUnusedCode};

/// An ICMPv6 packet with a dynamic message type.
///
/// Unlike `IcmpPacket`, `Packet` only supports ICMPv6, and does not
/// require a static message type. Each enum variant contains an `IcmpPacket` of
/// the appropriate static type, making it easier to call `parse` without
/// knowing the message type ahead of time while still getting the benefits of a
/// statically-typed packet struct after parsing is complete.
#[allow(missing_docs)]
pub enum Packet<B> {
    DestUnreachable(IcmpPacket<Ipv6, B, IcmpDestUnreachable>),
    PacketTooBig(IcmpPacket<Ipv6, B, Icmpv6PacketTooBig>),
    TimeExceeded(IcmpPacket<Ipv6, B, IcmpTimeExceeded>),
    ParameterProblem(IcmpPacket<Ipv6, B, Icmpv6ParameterProblem>),
    EchoRequest(IcmpPacket<Ipv6, B, IcmpEchoRequest>),
    EchoReply(IcmpPacket<Ipv6, B, IcmpEchoReply>),
}

impl<B: ByteSlice> Packet<B> {
    /// Parse an ICMP packet.
    ///
    /// `parse` parses `bytes` as an ICMP packet and validates the header fields
    /// and checksum.  It returns the byte range corresponding to the message
    /// body within `bytes`. This can be useful when extracting the encapsulated
    /// body to send to another layer of the stack. If the message type has no
    /// body, then the range is meaningless and should be ignored.
    pub fn parse(
        bytes: B, src_ip: Ipv6Addr, dst_ip: Ipv6Addr,
    ) -> Result<(Packet<B>, Range<usize>), ParseError> {
        macro_rules! mtch {
            ($bytes:expr, $src_ip:expr, $dst_ip:expr, $($variant:ident => $type:ty,)*) => {
                match peek_message_type(&$bytes)? {
                    $(MessageType::$variant => {
                        let (packet, range) = IcmpPacket::<Ipv6, B, $type>::parse($bytes, $src_ip, $dst_ip)?;
                        (Packet::$variant(packet), range)
                    })*
                }
            }
        }

        Ok(mtch!(
            bytes,
            src_ip,
            dst_ip,
            DestUnreachable => IcmpDestUnreachable,
            PacketTooBig => Icmpv6PacketTooBig,
            TimeExceeded => IcmpTimeExceeded,
            ParameterProblem => Icmpv6ParameterProblem,
            EchoRequest => IcmpEchoRequest,
            EchoReply => IcmpEchoReply,
        ))
    }
}

create_net_enum! {
    MessageType,
    DestUnreachable: DEST_UNREACHABLE = 1,
    PacketTooBig: PACKET_TOO_BIG = 2,
    TimeExceeded: TIME_EXCEEDED = 3,
    ParameterProblem: PARAMETER_PROBLEM = 4,
    EchoRequest: ECHO_REQUEST = 128,
    EchoReply: ECHO_REPLY = 129,
}

impl_icmp_message!(Ipv6, IcmpEchoRequest, EchoRequest, IcmpUnusedCode, true);

impl_icmp_message!(Ipv6, IcmpEchoReply, EchoReply, IcmpUnusedCode, true);

create_net_enum! {
  Icmpv6DestUnreachableCode,
  NoRoute: NO_ROUTE = 0,
  CommAdministrativelyProhibited: COMM_ADMINISTRATIVELY_PROHIBITED = 1,
  BeyondScope: BEYOND_SCOPE = 2,
  AddrUnreachable: ADDR_UNREACHABLE = 3,
  PortUnreachable: PORT_UNREACHABLE = 4,
  SrcAddrFailedPolicy: SRC_ADDR_FAILED_POLICY = 5,
  RejectRoute: REJECT_ROUTE = 6,
}

impl_icmp_message!(
    Ipv6,
    IcmpDestUnreachable,
    DestUnreachable,
    Icmpv6DestUnreachableCode,
    true
);

#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv6PacketTooBig {
    MTU: [u8; 4],
}

impl HasOriginalPacket for Icmpv6PacketTooBig {}
impl_from_bytes_as_bytes_unaligned!(Icmpv6PacketTooBig);
impl_icmp_message!(Ipv6, Icmpv6PacketTooBig, PacketTooBig, IcmpUnusedCode, true);

create_net_enum! {
  Icmpv6TimeExceededCode,
  HopLimitExceeded: HOP_LIMIT_EXCEEDED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

impl_icmp_message!(
    Ipv6,
    IcmpTimeExceeded,
    TimeExceeded,
    Icmpv6TimeExceededCode,
    true
);

create_net_enum! {
  Icmpv6ParameterProblemCode,
  ErroneousHeaderField: ERRONEOUS_HEADER_FIELD = 0,
  UnrecognizedNextHeaderType: UNRECOGNIZED_NEXT_HEADER_TYPE = 1,
  UnrecognizedIpv6Option: UNRECOGNIZED_IPV6_OPTION = 2,
}

/// An ICMPv6 Parameter Problem message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv6ParameterProblem {
    pointer: [u8; 4],
}

impl HasOriginalPacket for Icmpv6ParameterProblem {}

impl_from_bytes_as_bytes_unaligned!(Icmpv6ParameterProblem);

impl_icmp_message!(
    Ipv6,
    Icmpv6ParameterProblem,
    ParameterProblem,
    Icmpv6ParameterProblemCode,
    true
);

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wire::icmp::{IcmpMessage, IcmpPacket};
    use crate::wire::ipv4::{Ipv4Packet, Ipv4PacketSerializer};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketSerializer};
    use crate::wire::util::{BufferAndRange, PacketSerializer, SerializationRequest};

    fn serialize_to_bytes<B: ByteSlice, M: IcmpMessage<Ipv6>>(
        src_ip: Ipv6Addr, dst_ip: Ipv6Addr, icmp: &IcmpPacket<Ipv6, B, M>,
        serializer: Ipv6PacketSerializer,
    ) -> Vec<u8> {
        let icmp_serializer = icmp.serializer(src_ip, dst_ip);
        let mut data = vec![0; icmp_serializer.max_header_bytes() + icmp.message_body.len()];
        let body_offset = data.len() - icmp.message_body.len();
        (&mut data[body_offset..]).copy_from_slice(&icmp.message_body);
        BufferAndRange::new_from(&mut data[..], body_offset..)
            .encapsulate(icmp_serializer)
            .encapsulate(serializer)
            .serialize_outer()
            .as_ref()
            .to_vec()
    }

    #[test]
    fn test_parse_and_serialize_echo_request_ipv6() {
        use crate::wire::testdata::icmp_echo_v6::*;
        let (ip, _) = Ipv6Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, hop_limit) = (ip.src_ip(), ip.dst_ip(), ip.hop_limit());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpEchoRequest>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.message_body(), ECHO_DATA);
        assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], REQUEST_IP_PACKET_BYTES);
    }
}
