// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ICMPv4

use std::fmt;

use net_types::ip::{Ipv4, Ipv4Addr};
use packet::{BufferView, ParsablePacket, ParseMetadata};
use zerocopy::{AsBytes, ByteSlice, FromBytes, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::wire::U32;

use super::common::{IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpTimeExceeded};
use super::{
    peek_message_type, IcmpIpExt, IcmpMessageType, IcmpPacket, IcmpParseArgs, IcmpUnusedCode,
    IdAndSeq, OriginalPacket,
};

/// An ICMPv4 packet with a dynamic message type.
///
/// Unlike `IcmpPacket`, `Packet` only supports ICMPv4, and does not
/// require a static message type. Each enum variant contains an `IcmpPacket` of
/// the appropriate static type, making it easier to call `parse` without
/// knowing the message type ahead of time while still getting the benefits of a
/// statically-typed packet struct after parsing is complete.
#[allow(missing_docs)]
pub(crate) enum Icmpv4Packet<B: ByteSlice> {
    EchoReply(IcmpPacket<Ipv4, B, IcmpEchoReply>),
    DestUnreachable(IcmpPacket<Ipv4, B, IcmpDestUnreachable>),
    Redirect(IcmpPacket<Ipv4, B, Icmpv4Redirect>),
    EchoRequest(IcmpPacket<Ipv4, B, IcmpEchoRequest>),
    TimeExceeded(IcmpPacket<Ipv4, B, IcmpTimeExceeded>),
    ParameterProblem(IcmpPacket<Ipv4, B, Icmpv4ParameterProblem>),
    TimestampRequest(IcmpPacket<Ipv4, B, Icmpv4TimestampRequest>),
    TimestampReply(IcmpPacket<Ipv4, B, Icmpv4TimestampReply>),
}

impl<B: ByteSlice + fmt::Debug> fmt::Debug for Icmpv4Packet<B> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use self::Icmpv4Packet::*;
        match self {
            DestUnreachable(ref p) => f.debug_tuple("DestUnreachable").field(p).finish(),
            EchoReply(ref p) => f.debug_tuple("EchoReply").field(p).finish(),
            EchoRequest(ref p) => f.debug_tuple("EchoRequest").field(p).finish(),
            ParameterProblem(ref p) => f.debug_tuple("ParameterProblem").field(p).finish(),
            Redirect(ref p) => f.debug_tuple("Redirect").field(p).finish(),
            TimeExceeded(ref p) => f.debug_tuple("TimeExceeded").field(p).finish(),
            TimestampReply(ref p) => f.debug_tuple("TimestampReply").field(p).finish(),
            TimestampRequest(ref p) => f.debug_tuple("TimestampRequest").field(p).finish(),
        }
    }
}

impl<B: ByteSlice> ParsablePacket<B, IcmpParseArgs<Ipv4Addr>> for Icmpv4Packet<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        use self::Icmpv4Packet::*;
        match self {
            EchoReply(p) => p.parse_metadata(),
            DestUnreachable(p) => p.parse_metadata(),
            Redirect(p) => p.parse_metadata(),
            EchoRequest(p) => p.parse_metadata(),
            TimeExceeded(p) => p.parse_metadata(),
            ParameterProblem(p) => p.parse_metadata(),
            TimestampRequest(p) => p.parse_metadata(),
            TimestampReply(p) => p.parse_metadata(),
        }
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: IcmpParseArgs<Ipv4Addr>) -> ParseResult<Self> {
        macro_rules! mtch {
            ($buffer:expr, $args:expr, $($variant:ident => $type:ty,)*) => {
                match peek_message_type($buffer.as_ref())? {
                    $(Icmpv4MessageType::$variant => {
                        let packet = <IcmpPacket<Ipv4, B, $type> as ParsablePacket<_, _>>::parse($buffer, $args)?;
                        Icmpv4Packet::$variant(packet)
                    })*
                }
            }
        }

        Ok(mtch!(
            buffer,
            args,
            EchoReply => IcmpEchoReply,
            DestUnreachable => IcmpDestUnreachable,
            Redirect => Icmpv4Redirect,
            EchoRequest => IcmpEchoRequest,
            TimeExceeded => IcmpTimeExceeded,
            ParameterProblem => Icmpv4ParameterProblem,
            TimestampRequest => Icmpv4TimestampRequest,
            TimestampReply  => Icmpv4TimestampReply,
        ))
    }
}

create_net_enum! {
    pub Icmpv4MessageType,
    EchoReply: ECHO_REPLY = 0,
    DestUnreachable: DEST_UNREACHABLE = 3,
    Redirect: REDIRECT = 5,
    EchoRequest: ECHO_REQUEST = 8,
    TimeExceeded: TIME_EXCEEDED = 11,
    ParameterProblem: PARAMETER_PROBLEM = 12,
    TimestampRequest: TIMESTAMP_REQUEST = 13,
    TimestampReply: TIMESTAMP_REPLY = 14,
}

impl IcmpMessageType for Icmpv4MessageType {
    fn is_err(self) -> bool {
        use Icmpv4MessageType::*;
        [DestUnreachable, Redirect, TimeExceeded, ParameterProblem].contains(&self)
    }
}

create_net_enum! {
  pub Icmpv4DestUnreachableCode,
  DestNetworkUnreachable: DEST_NETWORK_UNREACHABLE = 0,
  DestHostUnreachable: DEST_HOST_UNREACHABLE = 1,
  DestProtocolUnreachable: DEST_PROTOCOL_UNREACHABLE = 2,
  DestPortUnreachable: DEST_PORT_UNREACHABLE = 3,
  FragmentationRequired: FRAGMENTATION_REQUIRED = 4,
  SourceRouteFailed: SOURCE_ROUTE_FAILED = 5,
  DestNetworkUnknown: DEST_NETWORK_UNKNOWN = 6,
  DestHostUnknown: DEST_HOST_UNKNOWN = 7,
  SourceHostIsolated: SOURCE_HOST_ISOLATED = 8,
  NetworkAdministrativelyProhibited: NETWORK_ADMINISTRATIVELY_PROHIBITED = 9,
  HostAdministrativelyProhibited: HOST_ADMINISTRATIVELY_PROHIBITED = 10,
  NetworkUnreachableForToS: NETWORK_UNREACHABLE_FOR_TOS = 11,
  HostUnreachableForToS: HOST_UNREACHABLE_FOR_TOS = 12,
  CommAdministrativelyProhibited: COMM_ADMINISTRATIVELY_PROHIBITED = 13,
  HostPrecedenceViolation: HOST_PRECEDENCE_VIOLATION = 14,
  PrecedenceCutoffInEffect: PRECEDENCE_CUTOFF_IN_EFFECT = 15,
}

impl_icmp_message!(
    Ipv4,
    IcmpDestUnreachable,
    DestUnreachable,
    Icmpv4DestUnreachableCode,
    OriginalPacket<B>
);
impl_icmp_message!(Ipv4, IcmpEchoRequest, EchoRequest, IcmpUnusedCode, OriginalPacket<B>);
impl_icmp_message!(Ipv4, IcmpEchoReply, EchoReply, IcmpUnusedCode, OriginalPacket<B>);

create_net_enum! {
  pub Icmpv4RedirectCode,
  Network: NETWORK = 0,
  Host: HOST = 1,
  ToSNetwork: TOS_NETWORK = 2,
  ToSHost: TOS_HOST = 3,
}

/// An ICMPv4 Redirect Message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Icmpv4Redirect {
    gateway: Ipv4Addr,
}

impl_icmp_message!(Ipv4, Icmpv4Redirect, Redirect, Icmpv4RedirectCode, OriginalPacket<B>);

create_net_enum! {
  pub Icmpv4TimeExceededCode,
  TtlExpired: TTL_EXPIRED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

impl_icmp_message!(Ipv4, IcmpTimeExceeded, TimeExceeded, Icmpv4TimeExceededCode, OriginalPacket<B>);

#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[cfg_attr(test, derive(Eq, PartialEq))]
#[repr(C)]
struct IcmpTimestampData {
    origin_timestamp: U32,
    recv_timestamp: U32,
    tx_timestamp: U32,
}

#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[cfg_attr(test, derive(Eq, PartialEq))]
#[repr(C)]
struct Timestamp {
    id_seq: IdAndSeq,
    timestamps: IcmpTimestampData,
}

/// An ICMPv4 Timestamp Request message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(transparent)]
pub(crate) struct Icmpv4TimestampRequest(Timestamp);

impl Icmpv4TimestampRequest {
    /// Creates an `Icmpv4TimestampRequest`.
    ///
    /// `new` constructs a new `Icmpv4TimestampRequest` with the given
    /// parameters, and sets the Receive Timestamp and Transmit Timestamp values
    /// to zero.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn new(origin_timestamp: u32, id: u16, seq: u16) -> Icmpv4TimestampRequest {
        Icmpv4TimestampRequest(Timestamp {
            id_seq: IdAndSeq::new(id, seq),
            timestamps: IcmpTimestampData {
                origin_timestamp: U32::new(origin_timestamp),
                recv_timestamp: U32::ZERO,
                tx_timestamp: U32::ZERO,
            },
        })
    }

    /// Reply to a Timestamp Request message.
    ///
    /// `reply` takes the `Icmpv4TimestampRequest` from a Timestamp Request
    /// message, and produces the appropriate `Icmpv4TimestampReply` value for a
    /// Timestamp Reply message. The original Originate Timestamp, ICMP ID, and
    /// ICMP Sequence Number are retained, while the Receive Timestamp and
    /// Transmit Timestamp are overwritten with the given values.
    ///
    /// The Receive Timestamp (`recv_timestamp`) indicates the time at which the
    /// Timestamp Request was first received, while the Transmit Timestamp
    /// (`tx_timestamp`) indicates the time at which the Timestamp Reply was
    /// last processed before being sent.
    pub(crate) fn reply(&self, recv_timestamp: u32, tx_timestamp: u32) -> Icmpv4TimestampReply {
        let mut ret = self.0.clone();
        ret.timestamps.recv_timestamp = U32::new(recv_timestamp);
        ret.timestamps.tx_timestamp = U32::new(tx_timestamp);
        Icmpv4TimestampReply(ret)
    }
}

/// An ICMPv4 Timestamp Reply message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[cfg_attr(test, derive(Eq, PartialEq))]
#[repr(transparent)]
pub(crate) struct Icmpv4TimestampReply(Timestamp);

impl_icmp_message!(Ipv4, Icmpv4TimestampRequest, TimestampRequest, IcmpUnusedCode);
impl_icmp_message!(Ipv4, Icmpv4TimestampReply, TimestampReply, IcmpUnusedCode);

create_net_enum! {
  pub Icmpv4ParameterProblemCode,
  PointerIndicatesError: POINTER_INDICATES_ERROR = 0,
  MissingRequiredOption: MISSING_REQUIRED_OPTION = 1,
  BadLength: BAD_LENGTH = 2,
}

/// An ICMPv4 Parameter Problem message.
#[derive(Copy, Clone, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct Icmpv4ParameterProblem {
    pointer: u8,
    _unused: [u8; 3],
    /* The rest of Icmpv4ParameterProblem is variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl Icmpv4ParameterProblem {
    pub(crate) fn new(pointer: u8) -> Icmpv4ParameterProblem {
        Icmpv4ParameterProblem { pointer, _unused: [0; 3] }
    }
}

impl_icmp_message!(
    Ipv4,
    Icmpv4ParameterProblem,
    ParameterProblem,
    Icmpv4ParameterProblemCode,
    OriginalPacket<B>
);

#[cfg(test)]
mod tests {
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};
    use std::fmt::Debug;

    use super::*;
    use crate::wire::icmp::{IcmpMessage, MessageBody};
    use crate::wire::ipv4::{Ipv4Header, Ipv4Packet, Ipv4PacketBuilder};

    fn serialize_to_bytes<B: ByteSlice + Debug, M: IcmpMessage<Ipv4, B> + Debug>(
        src_ip: Ipv4Addr,
        dst_ip: Ipv4Addr,
        icmp: &IcmpPacket<Ipv4, B, M>,
        builder: Ipv4PacketBuilder,
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
        M: for<'a> IcmpMessage<Ipv4, &'a [u8]> + Debug,
        F: for<'a> FnOnce(&IcmpPacket<Ipv4, &'a [u8], M>),
    >(
        mut req: &[u8],
        check: F,
    ) {
        let orig_req = &req[..];

        let ip = req.parse::<Ipv4Packet<_>>().unwrap();
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
        use crate::wire::testdata::icmp_echo::*;
        test_parse_and_serialize::<IcmpEchoRequest, _>(REQUEST_IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.message_body.bytes(), ECHO_DATA);
            assert_eq!(icmp.message().id_seq.id.get(), IDENTIFIER);
            assert_eq!(icmp.message().id_seq.seq.get(), SEQUENCE_NUM);
        });
    }

    #[test]
    fn test_parse_and_serialize_echo_response() {
        use crate::wire::testdata::icmp_echo::*;
        test_parse_and_serialize::<IcmpEchoReply, _>(RESPONSE_IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.message_body.bytes(), ECHO_DATA);
            assert_eq!(icmp.message().id_seq.id.get(), IDENTIFIER);
            assert_eq!(icmp.message().id_seq.seq.get(), SEQUENCE_NUM);
        });
    }

    #[test]
    fn test_parse_and_serialize_timestamp_request() {
        use crate::wire::testdata::icmp_timestamp::*;
        test_parse_and_serialize::<Icmpv4TimestampRequest, _>(REQUEST_IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.message().0.timestamps.origin_timestamp.get(), ORIGIN_TIMESTAMP);
            assert_eq!(icmp.message().0.timestamps.tx_timestamp.get(), RX_TX_TIMESTAMP);
            assert_eq!(icmp.message().0.id_seq.id.get(), IDENTIFIER);
            assert_eq!(icmp.message().0.id_seq.seq.get(), SEQUENCE_NUM);
        });
    }

    #[test]
    fn test_parse_and_serialize_timestamp_reply() {
        use crate::wire::testdata::icmp_timestamp::*;
        test_parse_and_serialize::<Icmpv4TimestampReply, _>(RESPONSE_IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.message().0.timestamps.origin_timestamp.get(), ORIGIN_TIMESTAMP);
            // TODO: Assert other values here?
            // TODO: Check value of recv_timestamp and tx_timestamp
            assert_eq!(icmp.message().0.id_seq.id.get(), IDENTIFIER);
            assert_eq!(icmp.message().0.id_seq.seq.get(), SEQUENCE_NUM);
        });
    }

    #[test]
    fn test_parse_and_serialize_dest_unreachable() {
        use crate::wire::testdata::icmp_dest_unreachable::*;
        test_parse_and_serialize::<IcmpDestUnreachable, _>(IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.code(), Icmpv4DestUnreachableCode::DestHostUnreachable);
            assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);
        });
    }

    #[test]
    fn test_parse_and_serialize_redirect() {
        use crate::wire::testdata::icmp_redirect::*;
        test_parse_and_serialize::<Icmpv4Redirect, _>(IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.code(), Icmpv4RedirectCode::Host);
            assert_eq!(icmp.message().gateway, GATEWAY_ADDR);
        });
    }

    #[test]
    fn test_parse_and_serialize_time_exceeded() {
        use crate::wire::testdata::icmp_time_exceeded::*;
        test_parse_and_serialize::<IcmpTimeExceeded, _>(IP_PACKET_BYTES, |icmp| {
            assert_eq!(icmp.code(), Icmpv4TimeExceededCode::TtlExpired);
            assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);
        });
    }
}
