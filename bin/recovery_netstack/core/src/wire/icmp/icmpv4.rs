// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! ICMP v4

use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::ByteSlice;

use crate::error::ParseError;
use crate::ip::{Ipv4, Ipv4Addr};

use super::common::{IcmpDestUnreachable, IcmpEchoReply, IcmpEchoRequest, IcmpTimeExceeded};
use super::{peek_message_type, IcmpIpExt, IcmpPacket, IcmpUnusedCode, IdAndSeq, OriginalPacket};

/// An ICMPv4 packet with a dynamic message type.
///
/// Unlike `IcmpPacket`, `Packet` only supports ICMPv4, and does not
/// require a static message type. Each enum variant contains an `IcmpPacket` of
/// the appropriate static type, making it easier to call `parse` without
/// knowing the message type ahead of time while still getting the benefits of a
/// statically-typed packet struct after parsing is complete.
#[allow(missing_docs)]
pub enum Packet<B> {
    EchoReply(IcmpPacket<Ipv4, B, IcmpEchoReply>),
    DestUnreachable(IcmpPacket<Ipv4, B, IcmpDestUnreachable>),
    Redirect(IcmpPacket<Ipv4, B, Icmpv4Redirect>),
    EchoRequest(IcmpPacket<Ipv4, B, IcmpEchoRequest>),
    TimeExceeded(IcmpPacket<Ipv4, B, IcmpTimeExceeded>),
    ParameterProblem(IcmpPacket<Ipv4, B, Icmpv4ParameterProblem>),
    TimestampRequest(IcmpPacket<Ipv4, B, Icmpv4TimestampRequest>),
    TimestampReply(IcmpPacket<Ipv4, B, Icmpv4TimestampReply>),
}

create_net_enum! {
    MessageType,
    EchoReply: ECHO_REPLY = 0,
    DestUnreachable: DEST_UNREACHABLE = 3,
    Redirect: REDIRECT = 5,
    EchoRequest: ECHO_REQUEST = 8,
    TimeExceeded: TIME_EXCEEDED = 11,
    ParameterProblem: PARAMETER_PROBLEM = 12,
    TimestampRequest: TIMESTAMP_REQUEST = 13,
    TimestampReply: TIMESTAMP_REPLY = 14,
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
        bytes: B, src_ip: Ipv4Addr, dst_ip: Ipv4Addr,
    ) -> Result<(Packet<B>, Range<usize>), ParseError> {
        macro_rules! mtch {
            ($bytes:expr, $src_ip:expr, $dst_ip:expr, $($variant:ident => $type:ty,)*) => {
                match peek_message_type(&$bytes)? {
                    $(MessageType::$variant => {
                        let (packet, range) = IcmpPacket::<Ipv4, B, $type>::parse($bytes, $src_ip, $dst_ip)?;
                        (Packet::$variant(packet), range)
                    })*
                }
            }
        }

        Ok(mtch!(
            bytes,
            src_ip,
            dst_ip,
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
  Icmpv4DestUnreachableCode,
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
impl_icmp_message!(
    Ipv4,
    IcmpEchoRequest,
    EchoRequest,
    IcmpUnusedCode,
    OriginalPacket<B>
);
impl_icmp_message!(
    Ipv4,
    IcmpEchoReply,
    EchoReply,
    IcmpUnusedCode,
    OriginalPacket<B>
);

create_net_enum! {
  Icmpv4RedirectCode,
  RedirectForNetwork: REDIRECT_FOR_NETWORK = 0,
  RedirectForHost: REDIRECT_FOR_HOST = 1,
  RedirectForToSNetwork: REDIRECT_FOR_TOS_NETWORK = 2,
  RedirectForToSHost: REDIRECT_FOR_TOS_HOST = 3,
}

/// An ICMPv4 Redirect Message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv4Redirect {
    gateway: Ipv4Addr,
}

impl_from_bytes_as_bytes_unaligned!(Icmpv4Redirect);

impl_icmp_message!(
    Ipv4,
    Icmpv4Redirect,
    Redirect,
    Icmpv4RedirectCode,
    OriginalPacket<B>
);

create_net_enum! {
  Icmpv4TimeExceededCode,
  TTLExpired: TTL_EXPIRED = 0,
  FragmentReassemblyTimeExceeded: FRAGMENT_REASSEMBLY_TIME_EXCEEDED = 1,
}

impl_icmp_message!(
    Ipv4,
    IcmpTimeExceeded,
    TimeExceeded,
    Icmpv4TimeExceededCode,
    OriginalPacket<B>
);

#[derive(Copy, Clone)]
#[repr(C, packed)]
struct IcmpTimestampData {
    origin_timestamp: [u8; 4],
    recv_timestamp: [u8; 4],
    tx_timestamp: [u8; 4],
}

impl IcmpTimestampData {
    fn origin_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.origin_timestamp)
    }

    fn recv_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.recv_timestamp)
    }

    fn tx_timestamp(&self) -> u32 {
        NetworkEndian::read_u32(&self.tx_timestamp)
    }

    fn set_origin_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.origin_timestamp, timestamp)
    }

    fn set_recv_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.recv_timestamp, timestamp)
    }

    fn set_tx_timestamp(&mut self, timestamp: u32) {
        NetworkEndian::write_u32(&mut self.tx_timestamp, timestamp)
    }
}

impl_from_bytes_as_bytes_unaligned!(IcmpTimestampData);

#[derive(Copy, Clone)]
#[repr(C, packed)]
struct Timestamp {
    id_seq: IdAndSeq,
    timestamps: IcmpTimestampData,
}

/// An ICMPv4 Timestamp Request message.
#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct Icmpv4TimestampRequest(Timestamp);

/// An ICMPv4 Timestamp Reply message.
#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct Icmpv4TimestampReply(Timestamp);

impl_from_bytes_as_bytes_unaligned!(Icmpv4TimestampRequest);
impl_from_bytes_as_bytes_unaligned!(Icmpv4TimestampReply);

impl_icmp_message!(
    Ipv4,
    Icmpv4TimestampRequest,
    TimestampRequest,
    IcmpUnusedCode
);
impl_icmp_message!(Ipv4, Icmpv4TimestampReply, TimestampReply, IcmpUnusedCode);

create_net_enum! {
  Icmpv4ParameterProblemCode,
  PointerIndicatesError: POINTER_INDICATES_ERROR = 0,
  MissingRequiredOption: MISSING_REQUIRED_OPTION = 1,
  BadLength: BAD_LENGTH = 2,
}

/// An ICMPv4 Parameter Problem message.
#[derive(Copy, Clone)]
#[repr(C, packed)]
pub struct Icmpv4ParameterProblem {
    pointer: u8,
    _unused: [u8; 3],
    /* The rest of Icmpv4ParameterProblem is variable-length, so is stored in
     * the message_body field in IcmpPacket */
}

impl_from_bytes_as_bytes_unaligned!(Icmpv4ParameterProblem);

impl_icmp_message!(
    Ipv4,
    Icmpv4ParameterProblem,
    ParameterProblem,
    Icmpv4ParameterProblemCode,
    OriginalPacket<B>
);

#[cfg(test)]
mod test {
    use super::*;
    use crate::wire::icmp::{IcmpMessage, MessageBody};
    use crate::wire::ipv4::{Ipv4Packet, Ipv4PacketSerializer};
    use crate::wire::util::{BufferAndRange, PacketSerializer, SerializationRequest};

    fn serialize_to_bytes<B: ByteSlice, M: IcmpMessage<Ipv4, B>>(
        src_ip: Ipv4Addr, dst_ip: Ipv4Addr, icmp: &IcmpPacket<Ipv4, B, M>,
        serializer: Ipv4PacketSerializer,
    ) -> Vec<u8> {
        let icmp_serializer = icmp.serializer(src_ip, dst_ip);
        let mut data = vec![0; icmp_serializer.max_header_bytes() + icmp.message_body.len()];
        let body_offset = data.len() - icmp.message_body.len();
        (&mut data[body_offset..]).copy_from_slice(icmp.message_body.bytes());
        BufferAndRange::new_from(&mut data[..], body_offset..)
            .encapsulate(icmp_serializer)
            .encapsulate(serializer)
            .serialize_outer()
            .as_ref()
            .to_vec()
    }

    #[test]
    fn test_parse_and_serialize_echo_request() {
        use crate::wire::testdata::icmp_echo::*;
        let (ip, _) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpEchoRequest>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.original_packet().bytes(), ECHO_DATA);
        assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], REQUEST_IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_echo_response() {
        use crate::wire::testdata::icmp_echo::*;
        let (ip, _) = Ipv4Packet::parse(RESPONSE_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpEchoReply>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.original_packet().bytes(), ECHO_DATA);
        assert_eq!(icmp.message().id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], RESPONSE_IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_timestamp_request() {
        use crate::wire::testdata::icmp_timestamp::*;
        let (ip, _) = Ipv4Packet::parse(REQUEST_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4TimestampRequest>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(
            icmp.message().0.timestamps.origin_timestamp(),
            ORIGIN_TIMESTAMP
        );
        assert_eq!(
            icmp.message().0.timestamps.recv_timestamp(),
            RX_TX_TIMESTAMP
        );
        assert_eq!(icmp.message().0.timestamps.tx_timestamp(), RX_TX_TIMESTAMP);
        assert_eq!(icmp.message().0.id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().0.id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], REQUEST_IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_timestamp_reply() {
        use crate::wire::testdata::icmp_timestamp::*;
        let (ip, _) = Ipv4Packet::parse(RESPONSE_IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4TimestampReply>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(
            icmp.message().0.timestamps.origin_timestamp(),
            ORIGIN_TIMESTAMP
        );
        // TODO: Assert other values here?
        // TODO: Check value of recv_timestamp and tx_timestamp
        assert_eq!(icmp.message().0.id_seq.id(), IDENTIFIER);
        assert_eq!(icmp.message().0.id_seq.seq(), SEQUENCE_NUM);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], RESPONSE_IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_dest_unreachable() {
        use crate::wire::testdata::icmp_dest_unreachable::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<Ipv4, _, IcmpDestUnreachable>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4DestUnreachableCode::DestHostUnreachable);
        assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_redirect() {
        use crate::wire::testdata::icmp_redirect::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, Icmpv4Redirect>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4RedirectCode::RedirectForHost);
        assert_eq!(icmp.message().gateway, GATEWAY_ADDR);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], IP_PACKET_BYTES);
    }

    #[test]
    fn test_parse_and_serialize_time_exceeded() {
        use crate::wire::testdata::icmp_time_exceeded::*;
        let (ip, _) = Ipv4Packet::parse(IP_PACKET_BYTES).unwrap();
        let (src_ip, dst_ip, ttl) = (ip.src_ip(), ip.dst_ip(), ip.ttl());
        // TODO: Check range
        let (icmp, _) =
            IcmpPacket::<_, _, IcmpTimeExceeded>::parse(ip.body(), src_ip, dst_ip).unwrap();
        assert_eq!(icmp.code(), Icmpv4TimeExceededCode::TTLExpired);
        assert_eq!(icmp.original_packet_body(), ORIGIN_DATA);

        let data = serialize_to_bytes(src_ip, dst_ip, &icmp, ip.serializer());
        assert_eq!(&data[..], IP_PACKET_BYTES);
    }

}
