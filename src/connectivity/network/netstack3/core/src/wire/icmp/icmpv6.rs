// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ICMPv6

use std::fmt;

use byteorder::{ByteOrder, NetworkEndian};
use packet::{BufferView, ParsablePacket, ParseMetadata};
use zerocopy::{AsBytes, ByteSlice, FromBytes, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{Ipv6, Ipv6Addr};

use super::common::{IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpTimeExceeded};
use super::{
    ndp, peek_message_type, IcmpIpExt, IcmpMessageType, IcmpPacket, IcmpParseArgs, IcmpUnusedCode,
    OriginalPacket,
};

/// An ICMPv6 packet with a dynamic message type.
///
/// Unlike `IcmpPacket`, `Packet` only supports ICMPv6, and does not
/// require a static message type. Each enum variant contains an `IcmpPacket` of
/// the appropriate static type, making it easier to call `parse` without
/// knowing the message type ahead of time while still getting the benefits of a
/// statically-typed packet struct after parsing is complete.
#[allow(missing_docs)]
pub(crate) enum Icmpv6Packet<B: ByteSlice> {
    DestUnreachable(IcmpPacket<Ipv6, B, IcmpDestUnreachable>),
    PacketTooBig(IcmpPacket<Ipv6, B, Icmpv6PacketTooBig>),
    TimeExceeded(IcmpPacket<Ipv6, B, IcmpTimeExceeded>),
    ParameterProblem(IcmpPacket<Ipv6, B, Icmpv6ParameterProblem>),
    EchoRequest(IcmpPacket<Ipv6, B, IcmpEchoRequest>),
    EchoReply(IcmpPacket<Ipv6, B, IcmpEchoReply>),
    RouterSolicitation(IcmpPacket<Ipv6, B, ndp::RouterSolicitation>),
    RouterAdvertisment(IcmpPacket<Ipv6, B, ndp::RouterAdvertisment>),
    NeighborSolicitation(IcmpPacket<Ipv6, B, ndp::NeighborSolicitation>),
    NeighborAdvertisment(IcmpPacket<Ipv6, B, ndp::NeighborAdvertisment>),
    Redirect(IcmpPacket<Ipv6, B, ndp::Redirect>),
}

impl<B: ByteSlice + fmt::Debug> fmt::Debug for Icmpv6Packet<B> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Icmpv6Packet::*;
        match self {
            DestUnreachable(ref p) => f.debug_tuple("DestUnreachable").field(p).finish(),
            PacketTooBig(ref p) => f.debug_tuple("PacketTooBig").field(p).finish(),
            TimeExceeded(ref p) => f.debug_tuple("TimeExceeded").field(p).finish(),
            ParameterProblem(ref p) => f.debug_tuple("ParameterProblem").field(p).finish(),
            EchoRequest(ref p) => f.debug_tuple("EchoRequest").field(p).finish(),
            EchoReply(ref p) => f.debug_tuple("EchoReply").field(p).finish(),
            RouterSolicitation(ref p) => f.debug_tuple("RouterSolicitation").field(p).finish(),
            RouterAdvertisment(ref p) => f.debug_tuple("RouterAdvertisment").field(p).finish(),
            NeighborSolicitation(ref p) => f.debug_tuple("NeighborSolicitation").field(p).finish(),
            NeighborAdvertisment(ref p) => f.debug_tuple("NeighborAdvertisment").field(p).finish(),
            Redirect(ref p) => f.debug_tuple("Redirect").field(p).finish(),
        }
    }
}

impl<B: ByteSlice> ParsablePacket<B, IcmpParseArgs<Ipv6Addr>> for Icmpv6Packet<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        use self::Icmpv6Packet::*;
        match self {
            DestUnreachable(p) => p.parse_metadata(),
            PacketTooBig(p) => p.parse_metadata(),
            TimeExceeded(p) => p.parse_metadata(),
            ParameterProblem(p) => p.parse_metadata(),
            EchoRequest(p) => p.parse_metadata(),
            EchoReply(p) => p.parse_metadata(),
            RouterSolicitation(p) => p.parse_metadata(),
            RouterAdvertisment(p) => p.parse_metadata(),
            NeighborSolicitation(p) => p.parse_metadata(),
            NeighborAdvertisment(p) => p.parse_metadata(),
            Redirect(p) => p.parse_metadata(),
        }
    }

    fn parse<BV: BufferView<B>>(
        mut buffer: BV,
        args: IcmpParseArgs<Ipv6Addr>,
    ) -> ParseResult<Self> {
        macro_rules! mtch {
            ($buffer:expr, $args:expr, $($variant:ident => $type:ty,)*) => {
                match peek_message_type($buffer.as_ref())? {
                    $(Icmpv6MessageType::$variant => {
                        let packet = <IcmpPacket<Ipv6, B, $type> as ParsablePacket<_, _>>::parse($buffer, $args)?;
                        Icmpv6Packet::$variant(packet)
                    })*
                }
            }
        }

        Ok(mtch!(
            buffer,
            args,
            DestUnreachable => IcmpDestUnreachable,
            PacketTooBig => Icmpv6PacketTooBig,
            TimeExceeded => IcmpTimeExceeded,
            ParameterProblem => Icmpv6ParameterProblem,
            EchoRequest => IcmpEchoRequest,
            EchoReply => IcmpEchoReply,
            RouterSolicitation => ndp::RouterSolicitation,
            RouterAdvertisment => ndp::RouterAdvertisment,
            NeighborSolicitation => ndp::NeighborSolicitation,
            NeighborAdvertisment => ndp::NeighborAdvertisment,
            Redirect => ndp::Redirect,
        ))
    }
}

create_net_enum! {
    Icmpv6MessageType,
    DestUnreachable: DEST_UNREACHABLE = 1,
    PacketTooBig: PACKET_TOO_BIG = 2,
    TimeExceeded: TIME_EXCEEDED = 3,
    ParameterProblem: PARAMETER_PROBLEM = 4,
    EchoRequest: ECHO_REQUEST = 128,
    EchoReply: ECHO_REPLY = 129,

    // NDP messages
    RouterSolicitation: ROUTER_SOLICITATION = 133,
    RouterAdvertisment: ROUTER_ADVERTISMENT = 134,
    NeighborSolicitation: NEIGHBOR_SOLICITATION = 135,
    NeighborAdvertisment: NEIGHBOR_ADVERTISMENT = 136,
    Redirect: REDIRECT = 137,
}

impl IcmpMessageType for Icmpv6MessageType {
    fn is_err(self) -> bool {
        use Icmpv6MessageType::*;
        [DestUnreachable, PacketTooBig, TimeExceeded, ParameterProblem].contains(&self)
    }
}

impl_icmp_message!(Ipv6, IcmpEchoRequest, EchoRequest, IcmpUnusedCode, OriginalPacket<B>);

impl_icmp_message!(Ipv6, IcmpEchoReply, EchoReply, IcmpUnusedCode, OriginalPacket<B>);

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
    OriginalPacket<B>
);

#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Icmpv6PacketTooBig {
    mtu: [u8; 4],
}

impl Icmpv6PacketTooBig {
    pub(crate) fn new(mtu: u32) -> Icmpv6PacketTooBig {
        let mut buf = [0u8; 4];
        NetworkEndian::write_u32(&mut buf[..], mtu);
        Icmpv6PacketTooBig { mtu: buf }
    }
}

impl_icmp_message!(Ipv6, Icmpv6PacketTooBig, PacketTooBig, IcmpUnusedCode, OriginalPacket<B>);

create_net_enum! {
  Icmpv6TimeExceededCode,
  HopLimitExceeded: HOP_LIMIT_EXCEEDED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

impl_icmp_message!(Ipv6, IcmpTimeExceeded, TimeExceeded, Icmpv6TimeExceededCode, OriginalPacket<B>);

create_net_enum! {
  Icmpv6ParameterProblemCode,
  ErroneousHeaderField: ERRONEOUS_HEADER_FIELD = 0,
  UnrecognizedNextHeaderType: UNRECOGNIZED_NEXT_HEADER_TYPE = 1,
  UnrecognizedIpv6Option: UNRECOGNIZED_IPV6_OPTION = 2,
}

/// An ICMPv6 Parameter Problem message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Icmpv6ParameterProblem {
    pointer: [u8; 4],
}

impl Icmpv6ParameterProblem {
    pub(crate) fn new(pointer: u32) -> Icmpv6ParameterProblem {
        let mut buf = [0u8; 4];
        NetworkEndian::write_u32(&mut buf[..], pointer);
        Icmpv6ParameterProblem { pointer: buf }
    }

    pub(crate) fn pointer(&self) -> u32 {
        NetworkEndian::read_u32(&self.pointer)
    }
}

impl_icmp_message!(
    Ipv6,
    Icmpv6ParameterProblem,
    ParameterProblem,
    Icmpv6ParameterProblemCode,
    OriginalPacket<B>
);

#[cfg(test)]
mod tests {
    use packet::{ParseBuffer, Serializer};
    use std::fmt::Debug;

    use super::*;
    use crate::wire::icmp::{IcmpMessage, IcmpPacket, MessageBody};
    use crate::wire::ipv6::{Ipv6Packet, Ipv6PacketBuilder};

    fn serialize_to_bytes<B: ByteSlice + Debug, M: IcmpMessage<Ipv6, B> + Debug>(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        icmp: &IcmpPacket<Ipv6, B, M>,
        builder: Ipv6PacketBuilder,
    ) -> Vec<u8> {
        icmp.message_body
            .bytes()
            .encapsulate(icmp.builder(src_ip, dst_ip))
            .encapsulate(builder)
            .serialize_outer()
            .unwrap()
            .as_ref()
            .to_vec()
    }

    fn test_parse_and_serialize<
        M: for<'a> IcmpMessage<Ipv6, &'a [u8]> + Debug,
        F: for<'a> FnOnce(&IcmpPacket<Ipv6, &'a [u8], M>),
    >(
        mut req: &[u8],
        check: F,
    ) {
        let orig_req = &req[..];

        let ip = req.parse::<Ipv6Packet<_>>().unwrap();
        let mut body = ip.body();
        let icmp = body
            .parse_with::<_, IcmpPacket<_, _, M>>(IcmpParseArgs::new(ip.src_ip(), ip.dst_ip()))
            .unwrap();
        check(&icmp);

        let data = serialize_to_bytes(ip.src_ip(), ip.dst_ip(), &icmp, ip.builder());
        assert_eq!(&data[..], orig_req);
    }

    #[test]
    fn test_parse_and_serialize_echo_request() {
        use crate::wire::testdata::icmp_echo_v6::*;
        test_parse_and_serialize::<IcmpEchoRequest, _>(REQUEST_IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.message_body.bytes(), ECHO_DATA);
            assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
            assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);
        });
    }
}
