// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Internet Control Message Protocol (ICMP) packets.

#[macro_use]
mod macros;
mod common;
mod icmpv4;
mod icmpv6;
pub(crate) mod ndp;

#[cfg(test)]
mod testdata;

pub(crate) use self::common::*;
pub(crate) use self::icmpv4::*;
pub(crate) use self::icmpv6::*;

use std::cmp;
use std::convert::{TryFrom, TryInto};
use std::fmt::{self, Debug};
use std::marker::PhantomData;
use std::mem;
use std::ops::Deref;

use byteorder::{ByteOrder, NetworkEndian};
use never::Never;
use packet::{BufferView, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{Ip, IpAddress, IpProto, Ipv4, Ipv6};
use crate::wire::ipv4;
use crate::wire::util::checksum::Checksum;
use crate::wire::util::records::options::{Options, OptionsImpl};

#[derive(Default, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct Header {
    msg_type: u8,
    code: u8,
    checksum: [u8; 2],
    /* NOTE: The "Rest of Header" field is stored in message types rather than
     * in the Header. This helps consolidate how callers access data about the
     * packet, and is consistent with ICMPv6, which treats the field as part of
     * messages rather than the header. */
}

impl Header {
    fn set_msg_type<T: Into<u8>>(&mut self, msg_type: T) {
        self.msg_type = msg_type.into();
    }

    fn checksum(&self) -> u16 {
        NetworkEndian::read_u16(&self.checksum)
    }

    fn set_checksum(&mut self, checksum: u16) {
        NetworkEndian::write_u16(&mut self.checksum[..], checksum);
    }
}

/// Peek at an ICMP header to see what message type is present.
///
/// Since `IcmpPacket` is statically typed with the message type expected, this
/// type must be known ahead of time before calling `parse`. If multiple
/// different types are valid in a given parsing context, and so the caller
/// cannot know ahead of time which type to use, `peek_message_type` can be used
/// to peek at the header first to figure out which static type should be used
/// in a subsequent call to `parse`.
///
/// Note that `peek_message_type` only inspects certain fields in the header,
/// and so `peek_message_type` succeeding does not guarantee that a subsequent
/// call to `parse` will also succeed.
pub(crate) fn peek_message_type<MessageType: TryFrom<u8>>(
    bytes: &[u8],
) -> ParseResult<MessageType> {
    let (header, _) = LayoutVerified::<_, Header>::new_unaligned_from_prefix(bytes)
        .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
    MessageType::try_from(header.msg_type).or_else(|_| {
        Err(debug_err!(
            ParseError::NotSupported,
            "unrecognized message type: {:x}",
            header.msg_type,
        ))
    })
}

/// An extension trait adding ICMP-related functionality to `Ipv4` and `Ipv6`.
pub(crate) trait IcmpIpExt<B: ByteSlice>: Ip {
    /// The type of ICMP messages.
    ///
    /// For `Ipv4`, this is `Icmpv4MessageType`, and for `Ipv6`, this is
    /// `Icmpv6MessageType`.
    type IcmpMessageType: IcmpMessageType;

    /// The type of ICMP Packet.
    ///
    /// For `Ipv4`, this is `Icmpv4Packet`, and for `Ipv6`, this is
    /// `Icmpv6Packet`.
    type Packet: IcmpPacketType<B, Self>;

    const IP_PROTO: IpProto;

    /// Compute the length of the header of the packet prefix stored in `bytes`.
    ///
    /// Given the prefix of a packet stored in `bytes`, compute the length of
    /// the header of that packet, or `bytes.len()` if `bytes` does not contain
    /// the entire header. If the version is IPv6, the returned length should
    /// include all extension headers.
    fn header_len(bytes: &[u8]) -> usize;
}

// A default implementation for any I: Ip. This is to convince the Rust compiler
// that, given an I: Ip, it's guaranteed to implement IcmpIpExt. We humans know
// that Ipv4 and Ipv6 are the only types implementing Ip and so, since we
// implement IcmpIpExt for both of these types, this is fine. The compiler isn't
// so smart. This implementation should never actually be used.
impl<B: ByteSlice, I: Ip> IcmpIpExt<B> for I {
    default type IcmpMessageType = Never;
    default type Packet = Never;

    // divide by 0 will only happen during constant evaluation, which will only
    // happen if this implementation is monomorphized, which should never happen
    default const IP_PROTO: IpProto = {
        #[allow(const_err, clippy::eq_op, clippy::erasing_op)]
        let _ = 0 / 0;
        IpProto::Other(255)
    };

    default fn header_len(bytes: &[u8]) -> usize {
        unreachable!()
    }
}

impl<B: ByteSlice> IcmpIpExt<B> for Ipv4 {
    type IcmpMessageType = Icmpv4MessageType;
    type Packet = Icmpv4Packet<B>;

    const IP_PROTO: IpProto = IpProto::Icmp;

    fn header_len(bytes: &[u8]) -> usize {
        if bytes.len() < ipv4::IPV4_MIN_HDR_LEN {
            return bytes.len();
        }
        let (header_prefix, _) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix(bytes).unwrap();
        cmp::min(header_prefix.ihl() as usize * 4, bytes.len())
    }
}

impl<B: ByteSlice> IcmpIpExt<B> for Ipv6 {
    type IcmpMessageType = Icmpv6MessageType;
    type Packet = Icmpv6Packet<B>;

    const IP_PROTO: IpProto = IpProto::Icmpv6;

    fn header_len(bytes: &[u8]) -> usize {
        // NOTE: We panic here rather than doing log_unimplemented! because
        // there's no sane default value for this function. If it's called, it
        // doesn't make sense for the program to continue executing; if we did,
        // it would cause bugs in the caller.
        unimplemented!()
    }
}

/// An ICMP or ICMPv6 packet
///
/// 'IcmpPacketType' is implemented by `Icmpv4Packet` and `Icmpv6Packet`
pub(crate) trait IcmpPacketType<B: ByteSlice, I: Ip>:
    Sized + ParsablePacket<B, IcmpParseArgs<I::Addr>, Error = ParseError>
{
}

impl<B: ByteSlice> IcmpPacketType<B, Ipv4> for Icmpv4Packet<B> {}

impl<B: ByteSlice> IcmpPacketType<B, Ipv6> for Icmpv6Packet<B> {}

// TODO(joshlf): Once we have generic associated types, refactor this so that we
// don't have to bind B ahead of time. Removing that requirement would make some
// APIs (in particular, IcmpPacketBuilder) simpler by removing the B parameter
// from them as well.

/// `MessageBody` represents the parsed body of the ICMP packet.
///
/// - For messages that expect no body, the `MessageBody` is of type `()`.
/// - For NDP messages, the `MessageBody` is of the type `ndp::Options`.
/// - For all other messages, the `MessageBody` will be of the type
///   `OriginalPacket`, which is a thin wrapper around `B`.
pub(crate) trait MessageBody<B>: Sized {
    const EXPECTS_BODY: bool = true;

    /// Parse the MessageBody from the provided bytes.
    fn parse(bytes: B) -> ParseResult<Self>
    where
        B: ByteSlice;

    /// The length of the underlying buffer.
    fn len(&self) -> usize
    where
        B: ByteSlice;

    /// Return the underlying bytes.
    fn bytes(&self) -> &[u8]
    where
        B: Deref<Target = [u8]>;
}

impl<B> MessageBody<B> for () {
    const EXPECTS_BODY: bool = false;

    fn parse(bytes: B) -> ParseResult<()>
    where
        B: ByteSlice,
    {
        if !bytes.is_empty() {
            return debug_err!(Err(ParseError::Format), "unexpected message body");
        }

        Ok(())
    }

    fn len(&self) -> usize {
        0
    }

    fn bytes(&self) -> &[u8] {
        &[]
    }
}

/// A thin wrapper around B which implements `MessageBody`.
#[derive(Debug)]
pub(crate) struct OriginalPacket<B>(B);

impl<B: ByteSlice + Deref<Target = [u8]>> OriginalPacket<B> {
    pub(crate) fn body<I: IcmpIpExt<B>>(&self) -> &[u8] {
        // TODO(joshlf): Can these debug_asserts be triggered by external input?
        let header_len = I::header_len(&self.0);
        debug_assert!(header_len <= self.0.len());
        debug_assert!(I::VERSION.is_v6() || self.0.len() - header_len == 8);
        &self.0[header_len..]
    }
}

impl<B> MessageBody<B> for OriginalPacket<B> {
    fn parse(bytes: B) -> ParseResult<OriginalPacket<B>> {
        Ok(OriginalPacket(bytes))
    }

    fn len(&self) -> usize
    where
        B: ByteSlice,
    {
        self.0.len()
    }

    fn bytes(&self) -> &[u8]
    where
        B: Deref<Target = [u8]>,
    {
        &self.0
    }
}

impl<B, O: for<'a> OptionsImpl<'a>> MessageBody<B> for Options<B, O> {
    fn parse(bytes: B) -> ParseResult<Options<B, O>>
    where
        B: ByteSlice,
    {
        Self::parse(bytes).map_err(|e| debug_err!(ParseError::Format, "unable to parse options"))
    }

    fn len(&self) -> usize
    where
        B: ByteSlice,
    {
        self.bytes().len()
    }

    fn bytes(&self) -> &[u8]
    where
        B: Deref<Target = [u8]>,
    {
        self.bytes()
    }
}

/// An ICMP message.
pub(crate) trait IcmpMessage<I: IcmpIpExt<B>, B: ByteSlice>:
    Sized + Copy + FromBytes + AsBytes + Unaligned
{
    /// The type of codes used with this message.
    ///
    /// The ICMP header includes an 8-bit "code" field. For a given message
    /// type, different values of this field carry different meanings. Not all
    /// code values are used - some may be invalid. This type representents a
    /// parsed code. For example, for TODO, it is the TODO type.
    type Code: Into<u8> + Copy + Debug;

    type Body: MessageBody<B>;

    /// The type corresponding to this message type.
    ///
    /// The value of the "type" field in the ICMP header corresponding to
    /// messages of this type.
    const TYPE: I::IcmpMessageType;

    /// Parse a `Code` from an 8-bit number.
    ///
    /// Parse a `Code` from the 8-bit "code" field in the ICMP header. Not all
    /// values for this field are valid. If an invalid value is passed,
    /// `code_from_u8` returns `None`.
    fn code_from_u8(code: u8) -> Option<Self::Code>;
}

/// The type of an ICMP message.
///
/// `IcmpMessageType` is implemented by `Icmpv4MessageType` and
/// `Icmpv6MessageType`.
pub trait IcmpMessageType: Into<u8> + Copy {
    /// Is this an error message?
    ///
    /// For ICMP, this is true for the Destination Unreachable, Redirect, Source
    /// Quench, Time Exceeded, and Parameter Problem message types. For ICMPv6,
    /// this is true for the Destination Unreachable, Packet Too Big, Time
    /// Exceeded, and Parameter Problem message types.
    fn is_err(self) -> bool;
}

/// An ICMP packet.
///
/// An `IcmpPacket` shares its underlying memory with the byte slice it was
/// parsed from, meaning that no copying or extra allocation is necessary.
pub(crate) struct IcmpPacket<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> {
    header: LayoutVerified<B, Header>,
    message: LayoutVerified<B, M>,
    message_body: M::Body,
    _marker: PhantomData<I>,
}

impl<
        I: IcmpIpExt<B>,
        B: ByteSlice,
        MB: fmt::Debug + MessageBody<B>,
        M: IcmpMessage<I, B, Body = MB> + fmt::Debug,
    > fmt::Debug for IcmpPacket<I, B, M>
{
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_struct("IcmpPacket")
            .field("header", &self.header)
            .field("message", &self.message)
            .field("message_body", &self.message_body)
            .finish()
    }
}

/// Arguments required to parse an ICMP packet.
pub(crate) struct IcmpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> IcmpParseArgs<A> {
    /// Construct a new `IcmpParseArgs`.
    pub(crate) fn new(src_ip: A, dst_ip: A) -> IcmpParseArgs<A> {
        IcmpParseArgs { src_ip, dst_ip }
    }
}

impl<B: ByteSlice, I: IcmpIpExt<B>, M: IcmpMessage<I, B>> ParsablePacket<B, IcmpParseArgs<I::Addr>>
    for IcmpPacket<I, B, M>
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.header.bytes().len() + self.message.bytes().len();
        ParseMetadata::from_packet(header_len, self.message_body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: IcmpParseArgs<I::Addr>) -> ParseResult<Self> {
        let header = buffer
            .take_obj_front::<Header>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let message = buffer
            .take_obj_front::<M>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for packet"))?;
        let message_body = buffer.into_rest();
        if !M::Body::EXPECTS_BODY && !message_body.is_empty() {
            return debug_err!(Err(ParseError::Format), "unexpected message body");
        }

        if header.msg_type != M::TYPE.into() {
            return debug_err!(Err(ParseError::NotExpected), "unexpected message type");
        }
        M::code_from_u8(header.code).ok_or_else(debug_err_fn!(
            ParseError::Format,
            "unrecognized code: {}",
            header.code
        ))?;
        if header.checksum()
            != Self::compute_checksum(
                &header,
                message.bytes(),
                &message_body,
                args.src_ip,
                args.dst_ip,
            )
            .ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?
        {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }
        let message_body = M::Body::parse(message_body)?;
        Ok(IcmpPacket { header, message, message_body, _marker: PhantomData })
    }
}

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacket<I, B, M> {
    /// Get the ICMP message.
    pub(crate) fn message(&self) -> &M {
        &self.message
    }

    /// Get the ICMP body.
    pub(crate) fn body(&self) -> &M::Body {
        &self.message_body
    }

    /// Get the ICMP message code.
    ///
    /// The code provides extra details about the message. Each message type has
    /// its own set of codes that are allowed.
    pub(crate) fn code(&self) -> M::Code {
        // infallible since it was validated in parse
        M::code_from_u8(self.header.code).unwrap()
    }

    /// Construct a builder with the same contents as this packet.
    pub(crate) fn builder(&self, src_ip: I::Addr, dst_ip: I::Addr) -> IcmpPacketBuilder<I, B, M> {
        IcmpPacketBuilder { src_ip, dst_ip, code: self.code(), msg: *self.message() }
    }
}

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacket<I, B, M> {
    /// Compute the checksum, skipping the checksum field itself.
    ///
    /// `compute_checksum` returns `None` if the version is IPv6 and the total
    /// ICMP packet length overflows a u32.
    fn compute_checksum(
        header: &Header,
        message: &[u8],
        message_body: &[u8],
        src_ip: I::Addr,
        dst_ip: I::Addr,
    ) -> Option<u16> {
        let mut c = Checksum::new();
        if I::VERSION.is_v6() {
            c.add_bytes(src_ip.bytes());
            c.add_bytes(dst_ip.bytes());
            let icmpv6_len = mem::size_of::<Header>() + message.len() + message_body.len();
            let mut len_bytes = [0; 4];
            NetworkEndian::write_u32(&mut len_bytes, icmpv6_len.try_into().ok()?);
            c.add_bytes(&len_bytes[..]);
            c.add_bytes(&[0, 0, 0]);
            c.add_bytes(&[IpProto::Icmpv6.into()]);
        }
        c.add_bytes(&[header.msg_type, header.code]);
        c.add_bytes(message);
        c.add_bytes(message_body);
        Some(c.checksum())
    }
}

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B, Body = OriginalPacket<B>>>
    IcmpPacket<I, B, M>
{
    /// Get the body of the packet that caused this ICMP message.
    ///
    /// This ICMP message contains some of the bytes of the packet that caused
    /// this message to be emitted. `original_packet_body` returns as much of
    /// the body of that packet as is contained in this message. For IPv4, this
    /// is guaranteed to be 8 bytes. For IPv6, there are no guarantees about the
    /// length.
    pub(crate) fn original_packet_body(&self) -> &[u8] {
        self.message_body.body::<I>()
    }

    pub(crate) fn original_packet(&self) -> &OriginalPacket<B> {
        &self.message_body
    }
}

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B, Body = ndp::Options<B>>>
    IcmpPacket<I, B, M>
{
    /// Get the pared list of NDP options from the ICMP message.
    pub(crate) fn ndp_options(&self) -> &ndp::Options<B> {
        &self.message_body
    }
}

/// A builder for ICMP packets.
#[derive(Debug)]
pub(crate) struct IcmpPacketBuilder<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> {
    src_ip: I::Addr,
    dst_ip: I::Addr,
    code: M::Code,
    msg: M,
}

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacketBuilder<I, B, M> {
    /// Construct a new `IcmpPacketBuilder`.
    pub(crate) fn new(
        src_ip: I::Addr,
        dst_ip: I::Addr,
        code: M::Code,
        msg: M,
    ) -> IcmpPacketBuilder<I, B, M> {
        IcmpPacketBuilder { src_ip, dst_ip, code, msg }
    }
}

// TODO(joshlf): Figure out a way to split body and non-body message types by
// trait and implement PacketBuilder for some and InnerPacketBuilder for others.

impl<I: IcmpIpExt<B>, B: ByteSlice, M: IcmpMessage<I, B>> PacketBuilder
    for IcmpPacketBuilder<I, B, M>
{
    fn header_len(&self) -> usize {
        mem::size_of::<Header>() + mem::size_of::<M>()
    }

    fn min_body_len(&self) -> usize {
        0
    }

    fn max_body_len(&self) -> usize {
        // This is to make sure the body length doesn't overflow the 32-bit
        // length field in the pseudo-header used for calculating the checksum.
        //
        // Note that, for messages that don't take bodies, it's important that
        // we don't just set this to 0. Trying to serialize a body in a message
        // type which doesn't take bodies is a programmer error, so we should
        // panic in that case. Setting the max_body_len to 0 would surface the
        // issue as an MTU error, which would hide the underlying problem.
        // Instead, we assert in serialize. Eventually, we will hopefully figure
        // out a way to implement InnerPacketBuilder (rather than PacketBuilder)
        // for these message types, and this won't be an issue anymore.
        std::u32::MAX as usize
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize(self, mut buffer: SerializeBuffer) {
        use packet::BufferViewMut;

        let (mut prefix, message_body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut prefix = &mut prefix;

        assert!(
            M::Body::EXPECTS_BODY || message_body.is_empty(),
            "body provided for message that doesn't take a body"
        );
        // SECURITY: Use _zero constructors to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let mut header =
            prefix.take_obj_front_zero::<Header>().expect("too few bytes for ICMP message");
        let mut message =
            prefix.take_obj_front_zero::<M>().expect("too few bytes for ICMP message");
        *message = self.msg;
        header.set_msg_type(M::TYPE);
        header.code = self.code.into();
        let checksum = IcmpPacket::<I, B, M>::compute_checksum(
            &header,
            message.bytes(),
            message_body,
            self.src_ip,
            self.dst_ip,
        )
        .unwrap_or_else(|| {
            panic!(
                "total ICMP packet length of {} overflows 32-bit length field of pseudo-header",
                header.bytes().len() + message.bytes().len() + message_body.len(),
            )
        });
        header.set_checksum(checksum);
    }
}

/// The type of ICMP codes that are unused.
///
/// Some ICMP messages do not use codes. In Rust, the `IcmpMessage::Code` type
/// associated with these messages is `IcmpUnusedCode`. The only valid numerical
/// value for this code is 0.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub(crate) struct IcmpUnusedCode;

impl Into<u8> for IcmpUnusedCode {
    fn into(self) -> u8 {
        0
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct IdAndSeq {
    id: [u8; 2],
    seq: [u8; 2],
}

impl IdAndSeq {
    fn new(id: u16, seq: u16) -> IdAndSeq {
        let mut id_bytes = [0; 2];
        let mut seq_bytes = [0; 2];
        NetworkEndian::write_u16(&mut id_bytes, id);
        NetworkEndian::write_u16(&mut seq_bytes, seq);
        IdAndSeq { id: id_bytes, seq: seq_bytes }
    }

    fn id(self) -> u16 {
        NetworkEndian::read_u16(&self.id)
    }

    fn set_id(&mut self, id: u16) {
        NetworkEndian::write_u16(&mut self.id, id);
    }

    fn seq(self) -> u16 {
        NetworkEndian::read_u16(&self.seq)
    }

    fn set_seq(&mut self, seq: u16) {
        NetworkEndian::write_u16(&mut self.seq, seq);
    }
}
