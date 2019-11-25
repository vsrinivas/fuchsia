// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ICMPv6

use std::fmt;

use net_types::ip::{Ipv6, Ipv6Addr};
use packet::{BufferView, ParsablePacket, ParseMetadata};
use zerocopy::{AsBytes, ByteSlice, FromBytes, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::wire::U32;

use super::common::{IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpTimeExceeded};
use super::{
    mld, ndp, peek_message_type, IcmpIpExt, IcmpMessageType, IcmpPacket, IcmpParseArgs,
    IcmpUnusedCode, OriginalPacket,
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
    Ndp(ndp::NdpPacket<B>),
    Mld(mld::MldPacket<B>),
}

impl<B: ByteSlice + fmt::Debug> fmt::Debug for Icmpv6Packet<B> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use self::Icmpv6Packet::*;
        use mld::MldPacket::*;
        use ndp::NdpPacket::*;
        match self {
            DestUnreachable(ref p) => f.debug_tuple("DestUnreachable").field(p).finish(),
            PacketTooBig(ref p) => f.debug_tuple("PacketTooBig").field(p).finish(),
            TimeExceeded(ref p) => f.debug_tuple("TimeExceeded").field(p).finish(),
            ParameterProblem(ref p) => f.debug_tuple("ParameterProblem").field(p).finish(),
            EchoRequest(ref p) => f.debug_tuple("EchoRequest").field(p).finish(),
            EchoReply(ref p) => f.debug_tuple("EchoReply").field(p).finish(),
            Ndp(RouterSolicitation(ref p)) => f.debug_tuple("RouterSolicitation").field(p).finish(),
            Ndp(RouterAdvertisement(ref p)) => {
                f.debug_tuple("RouterAdvertisement").field(p).finish()
            }
            Ndp(NeighborSolicitation(ref p)) => {
                f.debug_tuple("NeighborSolicitation").field(p).finish()
            }
            Ndp(NeighborAdvertisement(ref p)) => {
                f.debug_tuple("NeighborAdvertisement").field(p).finish()
            }
            Ndp(Redirect(ref p)) => f.debug_tuple("Redirect").field(p).finish(),
            Mld(MulticastListenerQuery(ref p)) => {
                f.debug_tuple("MulticastListenerQuery").field(p).finish()
            }
            Mld(MulticastListenerReport(ref p)) => {
                f.debug_tuple("MulticastListenerReport").field(p).finish()
            }
            Mld(MulticastListenerDone(ref p)) => {
                f.debug_tuple("MulticastListenerDone").field(p).finish()
            }
        }
    }
}

impl<B: ByteSlice> ParsablePacket<B, IcmpParseArgs<Ipv6Addr>> for Icmpv6Packet<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        use self::Icmpv6Packet::*;
        use mld::MldPacket::*;
        use ndp::NdpPacket::*;
        match self {
            DestUnreachable(p) => p.parse_metadata(),
            PacketTooBig(p) => p.parse_metadata(),
            TimeExceeded(p) => p.parse_metadata(),
            ParameterProblem(p) => p.parse_metadata(),
            EchoRequest(p) => p.parse_metadata(),
            EchoReply(p) => p.parse_metadata(),
            Ndp(RouterSolicitation(p)) => p.parse_metadata(),
            Ndp(RouterAdvertisement(p)) => p.parse_metadata(),
            Ndp(NeighborSolicitation(p)) => p.parse_metadata(),
            Ndp(NeighborAdvertisement(p)) => p.parse_metadata(),
            Ndp(Redirect(p)) => p.parse_metadata(),
            Mld(MulticastListenerQuery(p)) => p.parse_metadata(),
            Mld(MulticastListenerReport(p)) => p.parse_metadata(),
            Mld(MulticastListenerDone(p)) => p.parse_metadata(),
        }
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: IcmpParseArgs<Ipv6Addr>) -> ParseResult<Self> {
        use self::Icmpv6Packet::*;
        use mld::MldPacket::*;
        use ndp::NdpPacket::*;

        macro_rules! mtch {
            ($buffer:expr, $args:expr, $pkt_name:ident, $($msg_variant:ident => $pkt_variant:expr => $type:ty,)*) => {
                match peek_message_type($buffer.as_ref())? {
                    $(Icmpv6MessageType::$msg_variant => {
                        let $pkt_name = <IcmpPacket<Ipv6, B, $type> as ParsablePacket<_, _>>::parse($buffer, $args)?;
                        $pkt_variant
                    })*
                }
            }
        }

        Ok(mtch!(
            buffer,
            args,
            packet,
            DestUnreachable         => DestUnreachable(packet)              => IcmpDestUnreachable,
            PacketTooBig            => PacketTooBig(packet)                 => Icmpv6PacketTooBig,
            TimeExceeded            => TimeExceeded(packet)                 => IcmpTimeExceeded,
            ParameterProblem        => ParameterProblem(packet)             => Icmpv6ParameterProblem,
            EchoRequest             => EchoRequest(packet)                  => IcmpEchoRequest,
            EchoReply               => EchoReply(packet)                    => IcmpEchoReply,
            RouterSolicitation      => Ndp(RouterSolicitation(packet))      => ndp::RouterSolicitation,
            RouterAdvertisement     => Ndp(RouterAdvertisement(packet))     => ndp::RouterAdvertisement,
            NeighborSolicitation    => Ndp(NeighborSolicitation(packet))    => ndp::NeighborSolicitation,
            NeighborAdvertisement   => Ndp(NeighborAdvertisement(packet))   => ndp::NeighborAdvertisement,
            Redirect                => Ndp(Redirect(packet))                => ndp::Redirect,
            MulticastListenerQuery  => Mld(MulticastListenerQuery(packet))  => mld::MulticastListenerQuery,
            MulticastListenerReport => Mld(MulticastListenerReport(packet)) => mld::MulticastListenerReport,
            MulticastListenerDone   => Mld(MulticastListenerDone(packet))   => mld::MulticastListenerDone,
        ))
    }
}

create_net_enum! {
    pub Icmpv6MessageType,
    DestUnreachable: DEST_UNREACHABLE = 1,
    PacketTooBig: PACKET_TOO_BIG = 2,
    TimeExceeded: TIME_EXCEEDED = 3,
    ParameterProblem: PARAMETER_PROBLEM = 4,
    EchoRequest: ECHO_REQUEST = 128,
    EchoReply: ECHO_REPLY = 129,

    // NDP messages
    RouterSolicitation: ROUTER_SOLICITATION = 133,
    RouterAdvertisement: ROUTER_ADVERTISEMENT = 134,
    NeighborSolicitation: NEIGHBOR_SOLICITATION = 135,
    NeighborAdvertisement: NEIGHBOR_ADVERTISEMENT = 136,
    Redirect: REDIRECT = 137,

    // MLDv1 messages
    MulticastListenerQuery: MULTICAST_LISTENER_QUERY = 130,
    MulticastListenerReport: MULTICAST_LISTENER_REPORT = 131,
    MulticastListenerDone: MULTICAST_LISTENER_DONE = 132,
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
  pub Icmpv6DestUnreachableCode,
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

#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned, PartialEq)]
#[repr(C)]
pub(crate) struct Icmpv6PacketTooBig {
    mtu: U32,
}

impl Icmpv6PacketTooBig {
    pub(crate) fn new(mtu: u32) -> Icmpv6PacketTooBig {
        Icmpv6PacketTooBig { mtu: U32::new(mtu) }
    }

    /// Get the mtu value.
    pub(crate) fn mtu(&self) -> u32 {
        self.mtu.get()
    }
}

impl_icmp_message!(Ipv6, Icmpv6PacketTooBig, PacketTooBig, IcmpUnusedCode, OriginalPacket<B>);

create_net_enum! {
  pub Icmpv6TimeExceededCode,
  HopLimitExceeded: HOP_LIMIT_EXCEEDED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

impl_icmp_message!(Ipv6, IcmpTimeExceeded, TimeExceeded, Icmpv6TimeExceededCode, OriginalPacket<B>);

create_net_enum! {
  pub Icmpv6ParameterProblemCode,
  ErroneousHeaderField: ERRONEOUS_HEADER_FIELD = 0,
  UnrecognizedNextHeaderType: UNRECOGNIZED_NEXT_HEADER_TYPE = 1,
  UnrecognizedIpv6Option: UNRECOGNIZED_IPV6_OPTION = 2,
}

/// An ICMPv6 Parameter Problem message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Icmpv6ParameterProblem {
    pointer: U32,
}

impl Icmpv6ParameterProblem {
    pub(crate) fn new(pointer: u32) -> Icmpv6ParameterProblem {
        Icmpv6ParameterProblem { pointer: U32::new(pointer) }
    }

    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn pointer(self) -> u32 {
        self.pointer.get()
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
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};
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
            .into_serializer()
            .encapsulate(icmp.builder(src_ip, dst_ip))
            .encapsulate(builder)
            .serialize_vec_outer()
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
            assert_eq!(icmp.message().id_seq.id.get(), IDENTIFIER);
            assert_eq!(icmp.message().id_seq.seq.get(), SEQUENCE_NUM);
        });
    }
}
