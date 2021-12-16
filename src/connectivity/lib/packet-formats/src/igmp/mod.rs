// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Internet Group Management Protocol (IGMP)
//! packets.
//!
//! This module supports both IGMPv2 and IGMPv3.
//!
//! The IGMPv2 packet format is defined in [RFC 2236 Section 2], and the IGMPv3
//! packet format is defined in [RFC 3376 Section 4].
//!
//! [RFC 2236 Section 2]: https://datatracker.ietf.org/doc/html/rfc2236#section-2
//! [RFC 3376 Section 4]: https://datatracker.ietf.org/doc/html/rfc3376#section-4

pub mod messages;
mod types;

#[cfg(test)]
mod testdata;

pub use self::types::*;

use core::convert::TryFrom;
use core::fmt::Debug;
use core::marker::PhantomData;
use core::mem;

use internet_checksum::Checksum;
use net_types::ip::Ipv4Addr;
use packet::{
    AsFragmentedByteSlice, BufferView, FragmentedByteSlice, InnerPacketBuilder, PacketBuilder,
    PacketConstraints, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use self::messages::IgmpMessageType;
use crate::error::ParseError;

/// Trait specifying serialization behavior for IGMP messages.
///
/// IGMP messages are broken into 3 parts:
///
/// - `HeaderPrefix`: common to all IGMP messages;
/// - `FixedHeader`: A fixed part of the message following the HeaderPrefix;
/// - `VariableBody`: A variable-length body;
///
/// `MessageType` specifies the types used for `FixedHeader` and `VariableBody`,
/// `HeaderPrefix` is shared among all message types.
pub trait MessageType<B> {
    /// The fixed header type used for the message type.
    ///
    /// These are the bytes immediately following the checksum bytes in an IGMP
    /// message. Most IGMP messages' `FixedHeader` is an IPv4 address.
    type FixedHeader: Sized + Copy + Clone + FromBytes + AsBytes + Unaligned + Debug;

    /// The variable-length body for the message type.
    type VariableBody: Sized;

    /// The type corresponding to this message type.
    ///
    /// The value of the "type" field in the IGMP header corresponding to
    /// messages of this type.
    const TYPE: IgmpMessageType;

    /// A type specializing how to parse the `max_resp_code` field in the
    /// `HeaderPrefix`.
    ///
    /// Specifies how to transform *Max Resp Code* (transmitted value)
    /// into *Max Resp Time* (semantic meaning). Provides a single interface to
    /// deal with differences between IGMPv2 ([RFC 2236]) and
    /// IGMPv3 ([RFC 3376]).
    ///
    /// The *Max Resp Code* field in `HeaderPrefix` only has meaning for IGMP
    /// *Query* messages, and should be set to zero for all other outgoing
    /// and ignored for all other incoming messages. For that case,
    /// an implementation of `MaxRespTime` for `()` is provided.
    ///
    /// [RFC 2236]: https://tools.ietf.org/html/rfc2236
    /// [RFC 3376]: https://tools.ietf.org/html/rfc3376
    type MaxRespTime: Sized + IgmpMaxRespCode + Debug;

    /// Parses the variable body part of the IGMP message.
    fn parse_body<BV: BufferView<B>>(
        header: &Self::FixedHeader,
        bytes: BV,
    ) -> Result<Self::VariableBody, ParseError>
    where
        B: ByteSlice;

    /// Retrieves the underlying bytes of `VariableBody`.
    // Note: this is delegating the responsibility of getting
    // `VariableBody` as `[u8]` to `MessageType` as opposed to just enforcing
    // a trait in `VariableBody` to do so. The decision to go this way is to
    // be able to relax the `ByteSlice` requirement on `B` elsewhere and it
    // also plays better with existing traits on `LayoutVerified` and
    // `Records`.
    fn body_bytes(body: &Self::VariableBody) -> &[u8]
    where
        B: ByteSlice;
}

/// Trait for treating the max_resp_code field of `HeaderPrefix`.
///
/// There are parsing differences between IGMP v2 and v3  for the maximum
/// response code (in IGMP v2 is in fact called maximum response time). That's
/// the reasoning behind making this a trait, so it can be specialized
/// differently with thin wrappers by the messages implementation.
pub trait IgmpMaxRespCode {
    /// The serialized value of the response code
    fn as_code(&self) -> u8;
    /// Parses from a code value.
    fn from_code(code: u8) -> Self;
}

/// Max Resp Code should be ignored on all non-query incoming messages and
/// set to zero on all non-query outgoing messages, the implementation for `()`
/// provides that.
impl IgmpMaxRespCode for () {
    fn as_code(&self) -> u8 {
        0
    }

    fn from_code(_code: u8) {}
}

/// A marker trait for implementers of [`MessageType`]. Only [`MessageType`]s
/// whose `VariableBody` implements `IgmpNonEmptyBody` (or is `()` for empty
/// bodies) can be built using [`IgmpPacketBuilder`].
pub trait IgmpNonEmptyBody {}

/// A builder for IGMP packets.
#[derive(Debug)]
pub struct IgmpPacketBuilder<B, M: MessageType<B>> {
    max_resp_time: M::MaxRespTime,
    message_header: M::FixedHeader,
    _marker: PhantomData<B>,
}

impl<B, M: MessageType<B, MaxRespTime = ()>> IgmpPacketBuilder<B, M> {
    /// Construct a new `IgmpPacketBuilder`.
    pub fn new(msg_header: M::FixedHeader) -> IgmpPacketBuilder<B, M> {
        IgmpPacketBuilder { max_resp_time: (), message_header: msg_header, _marker: PhantomData }
    }
}

impl<B, M: MessageType<B>> IgmpPacketBuilder<B, M> {
    /// Construct a new `IgmpPacketBuilder` with provided `max_resp_time`.
    pub fn new_with_resp_time(
        msg_header: M::FixedHeader,
        max_resp_time: M::MaxRespTime,
    ) -> IgmpPacketBuilder<B, M> {
        IgmpPacketBuilder { max_resp_time, message_header: msg_header, _marker: PhantomData }
    }
}

impl<B, M: MessageType<B>> IgmpPacketBuilder<B, M> {
    fn serialize_headers<BB: packet::Fragment>(
        &self,
        mut headers_buff: &mut [u8],
        body: &FragmentedByteSlice<'_, BB>,
    ) {
        use packet::BufferViewMut;
        let mut bytes = &mut headers_buff;
        // SECURITY: Use _zero constructors to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let mut header_prefix =
            bytes.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for IGMP message");
        header_prefix.set_msg_type(M::TYPE);
        header_prefix.max_resp_code = self.max_resp_time.as_code();

        let mut header =
            bytes.take_obj_front_zero::<M::FixedHeader>().expect("too few bytes for IGMP message");
        *header = self.message_header;

        let checksum = compute_checksum_fragmented(&header_prefix, &header.bytes(), body);
        header_prefix.checksum = checksum;
    }
}

// All messages that do not have a VariableBody,
// can have an InnerPacketBuilder impl.
impl<B, M: MessageType<B, VariableBody = ()>> InnerPacketBuilder for IgmpPacketBuilder<B, M> {
    fn bytes_len(&self) -> usize {
        mem::size_of::<HeaderPrefix>() + mem::size_of::<M::FixedHeader>()
    }

    fn serialize(&self, buffer: &mut [u8]) {
        let empty = FragmentedByteSlice::<&'static [u8]>::new_empty();
        self.serialize_headers(buffer, &empty);
    }
}

impl<B, M: MessageType<B>> PacketBuilder for IgmpPacketBuilder<B, M>
where
    M::VariableBody: IgmpNonEmptyBody,
{
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            mem::size_of::<M::FixedHeader>() + mem::size_of::<HeaderPrefix>(),
            0,
            0,
            core::usize::MAX,
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (prefix, message_body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        self.serialize_headers(prefix, message_body);
    }
}

/// `HeaderPrefix` represents the first 4 octets of every IGMP message.
///
/// The `HeaderPrefix` carries the message type information, which is used to
/// parse which IGMP message follows.
///
/// Note that even though `max_rsp_time` is part of `HeaderPrefix`, it is not
/// meaningful or used in every message.
#[derive(Default, Debug, AsBytes, FromBytes, Unaligned)]
#[repr(C)]
pub struct HeaderPrefix {
    msg_type: u8,
    /// The Max Response Time field is meaningful only in Membership Query
    /// messages, and specifies the maximum allowed time before sending a
    /// responding report. In all other messages, it is set to zero by the
    /// sender and ignored by receivers. The parsing of `max_resp_code` into
    /// a value *Max Response Time* is performed by the `MaxRespType` type in
    /// the `MessageType` trait.
    max_resp_code: u8,
    checksum: [u8; 2],
}

impl HeaderPrefix {
    fn set_msg_type<T: Into<u8>>(&mut self, msg_type: T) {
        self.msg_type = msg_type.into();
    }
}

/// An IGMP message.
///
/// An `IgmpMessage` is a struct representing an IGMP message in memory;
/// it holds the 3 IGMP message parts and is characterized by the
/// `MessageType` trait.
#[derive(Debug)]
pub struct IgmpMessage<B: ByteSlice, M: MessageType<B>> {
    prefix: LayoutVerified<B, HeaderPrefix>,
    header: LayoutVerified<B, M::FixedHeader>,
    body: M::VariableBody,
}

impl<B: ByteSlice, M: MessageType<B>> IgmpMessage<B, M> {
    /// Construct a builder with the same contents as this packet.
    pub fn builder(&self) -> IgmpPacketBuilder<B, M> {
        IgmpPacketBuilder::new_with_resp_time(*self.header, self.max_response_time())
    }

    /// Gets the interpreted *Max Response Time* for the message
    pub fn max_response_time(&self) -> M::MaxRespTime {
        M::MaxRespTime::from_code(self.prefix.max_resp_code)
    }
}

fn compute_checksum_fragmented<BB: packet::Fragment>(
    header_prefix: &HeaderPrefix,
    header: &[u8],
    body: &FragmentedByteSlice<'_, BB>,
) -> [u8; 2] {
    let mut c = Checksum::new();
    c.add_bytes(&[header_prefix.msg_type, header_prefix.max_resp_code]);
    c.add_bytes(&header_prefix.checksum);
    c.add_bytes(header);
    for p in body.iter_fragments() {
        c.add_bytes(p);
    }
    c.checksum()
}

impl<B: ByteSlice, M: MessageType<B>> IgmpMessage<B, M> {
    fn compute_checksum(header_prefix: &HeaderPrefix, header: &[u8], body: &[u8]) -> [u8; 2] {
        let mut body = [body];
        compute_checksum_fragmented(header_prefix, header, &body.as_fragmented_byte_slice())
    }
}

impl<B: ByteSlice, M: MessageType<B, FixedHeader = Ipv4Addr>> IgmpMessage<B, M> {
    /// Returns the group address.
    pub fn group_addr(&self) -> Ipv4Addr {
        *self.header
    }
}

impl<B: ByteSlice, M: MessageType<B>> ParsablePacket<B, ()> for IgmpMessage<B, M> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.prefix.bytes().len() + self.header.bytes().len();
        ParseMetadata::from_packet(header_len, M::body_bytes(&self.body).len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, ParseError> {
        let prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header prefix"))?;

        let header = buffer
            .take_obj_front::<M::FixedHeader>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;

        let checksum = Self::compute_checksum(&prefix, &header.bytes(), buffer.as_ref());
        if checksum != [0, 0] {
            return debug_err!(
                Err(ParseError::Checksum),
                "invalid checksum, got 0x{:x?}",
                prefix.checksum
            );
        }

        if prefix.msg_type != M::TYPE.into() {
            return debug_err!(Err(ParseError::NotExpected), "unexpected message type");
        }

        let body = M::parse_body(&header, buffer)?;

        Ok(IgmpMessage { prefix, header, body })
    }
}

/// Peek at an IGMP header to see what message type is present.
///
/// Since `IgmpPacket` is statically typed with the message type expected, this
/// type must be known ahead of time before calling `parse`. If multiple
/// different types are valid in a given parsing context, and so the caller
/// cannot know ahead of time which type to use, `peek_message_type` can be used
/// to peek at the header first to figure out which static type should be used
/// in a subsequent call to `parse`.
///
/// Because IGMP reuses the message type field for different semantic
/// meanings between IGMP v2 and v3, `peek_message_type` also returns a boolean
/// indicating if it's a "long message", which should direct parsers into parsing
/// an IGMP v3 message.
///
/// Note that `peek_message_type` only inspects certain fields in the header,
/// and so `peek_message_type` succeeding does not guarantee that a subsequent
/// call to `parse` will also succeed.
pub fn peek_message_type<MessageType: TryFrom<u8>>(
    bytes: &[u8],
) -> Result<(MessageType, bool), ParseError> {
    // a long message is any message for which the size exceeds the common HeaderPrefix +
    // a single Ipv4Address
    let long_message =
        bytes.len() > (core::mem::size_of::<HeaderPrefix>() + core::mem::size_of::<Ipv4Addr>());
    let (header, _) = LayoutVerified::<_, HeaderPrefix>::new_unaligned_from_prefix(bytes)
        .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
    let msg_type = MessageType::try_from(header.msg_type).map_err(|_| {
        debug_err!(ParseError::NotSupported, "unrecognized message type: {:x}", header.msg_type,)
    })?;
    Ok((msg_type, long_message))
}

#[cfg(test)]
mod tests {
    use std::fmt::Debug;

    use packet::{ParseBuffer, Serializer};

    use super::*;
    use crate::igmp::messages::*;
    use crate::ip::Ipv4Proto;
    use crate::ipv4::{
        options::{Ipv4Option, Ipv4OptionData},
        Ipv4Header, Ipv4Packet, Ipv4PacketBuilder, Ipv4PacketBuilderWithOptions,
    };

    fn serialize_to_bytes<B: ByteSlice + Debug, M: MessageType<B, VariableBody = ()> + Debug>(
        igmp: &IgmpMessage<B, M>,
        src_ip: Ipv4Addr,
        dst_ip: Ipv4Addr,
    ) -> Vec<u8> {
        let ipv4 = Ipv4PacketBuilder::new(src_ip, dst_ip, 1, Ipv4Proto::Igmp);
        let with_options = Ipv4PacketBuilderWithOptions::new(
            ipv4,
            &[Ipv4Option { copied: true, data: Ipv4OptionData::RouterAlert { data: 0 } }],
        )
        .unwrap();

        igmp.builder()
            .into_serializer()
            .encapsulate(with_options)
            .serialize_vec_outer()
            .unwrap()
            .as_ref()
            .to_vec()
    }

    fn test_parse_and_serialize<
        M: for<'a> MessageType<&'a [u8], VariableBody = ()> + Debug,
        F: for<'a> FnOnce(&Ipv4Packet<&'a [u8]>),
        G: for<'a> FnOnce(&IgmpMessage<&'a [u8], M>),
    >(
        mut pkt: &[u8],
        check_ip: F,
        check_igmp: G,
    ) {
        let orig_req = pkt;

        let ip = pkt.parse_with::<_, Ipv4Packet<_>>(()).unwrap();
        let src_ip = ip.src_ip();
        let dst_ip = ip.dst_ip();
        check_ip(&ip);
        let mut req: &[u8] = pkt;
        let igmp = req.parse_with::<_, IgmpMessage<_, M>>(()).unwrap();
        check_igmp(&igmp);

        let data = serialize_to_bytes(&igmp, src_ip, dst_ip);
        assert_eq!(&data[..], orig_req);
    }

    // The following tests are basically still testing serialization and
    // parsing of IGMP messages. Besides IGMP messages themselves, the
    // following tests also test whether the RTRALRT option in the enclosing
    // IPv4 packet can be parsed/serialized correctly.

    #[test]
    fn test_parse_and_serialize_igmpv2_report_with_options() {
        use crate::testdata::igmpv2_membership::report::*;
        test_parse_and_serialize::<IgmpMembershipReportV2, _, _>(
            IP_PACKET_BYTES,
            |ip| {
                assert_eq!(ip.ttl(), 1);
                assert_eq!(ip.iter_options().count(), 1);
                let option = ip.iter_options().next().unwrap();
                assert!(option.copied);
                assert_eq!(option.data, Ipv4OptionData::RouterAlert { data: 0 });
                assert_eq!(ip.header_len(), 24);
                assert_eq!(ip.src_ip(), SOURCE);
                assert_eq!(ip.dst_ip(), MULTICAST);
            },
            |igmp| {
                assert_eq!(*igmp.header, MULTICAST);
            },
        )
    }

    #[test]
    fn test_parse_and_serialize_igmpv2_query_with_options() {
        use crate::testdata::igmpv2_membership::query::*;
        test_parse_and_serialize::<IgmpMembershipQueryV2, _, _>(
            IP_PACKET_BYTES,
            |ip| {
                assert_eq!(ip.ttl(), 1);
                assert_eq!(ip.iter_options().count(), 1);
                let option = ip.iter_options().next().unwrap();
                assert!(option.copied);
                assert_eq!(option.data, Ipv4OptionData::RouterAlert { data: 0 });
                assert_eq!(ip.header_len(), 24);
                assert_eq!(ip.src_ip(), SOURCE);
                assert_eq!(ip.dst_ip(), MULTICAST);
            },
            |igmp| {
                assert_eq!(*igmp.header, MULTICAST);
            },
        )
    }

    #[test]
    fn test_parse_and_serialize_igmpv2_leave_with_options() {
        use crate::testdata::igmpv2_membership::leave::*;
        test_parse_and_serialize::<IgmpLeaveGroup, _, _>(
            IP_PACKET_BYTES,
            |ip| {
                assert_eq!(ip.ttl(), 1);
                assert_eq!(ip.iter_options().count(), 1);
                let option = ip.iter_options().next().unwrap();
                assert!(option.copied);
                assert_eq!(option.data, Ipv4OptionData::RouterAlert { data: 0 });
                assert_eq!(ip.header_len(), 24);
                assert_eq!(ip.src_ip(), SOURCE);
                assert_eq!(ip.dst_ip(), DESTINATION);
            },
            |igmp| {
                assert_eq!(*igmp.header, MULTICAST);
            },
        )
    }
}
