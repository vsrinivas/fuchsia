// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Internet Control Message Protocol (ICMP)
//! packets.
//!
//! This module supports both ICMPv4 and ICMPv6.
//!
//! The ICMPv4 packet format is defined in [RFC 792], and the ICMPv6
//! packet format is defined in [RFC 4443 Section 2.1].
//!
//! [RFC 792]: https://datatracker.ietf.org/doc/html/rfc792
//! [RFC 4443 Section 2.1]: https://datatracker.ietf.org/doc/html/rfc4443#section-2.1

#[macro_use]
mod macros;
mod common;
mod icmpv4;
mod icmpv6;
pub mod mld;
pub mod ndp;

#[cfg(test)]
mod testdata;

pub use self::common::*;
pub use self::icmpv4::*;
pub use self::icmpv6::*;

use core::cmp;
use core::convert::{TryFrom, TryInto};
use core::fmt::Debug;
use core::marker::PhantomData;
use core::mem;
use core::ops::Deref;

use internet_checksum::Checksum;
use net_types::ip::{Ip, IpAddress, Ipv4, Ipv6};
use packet::records::options::{Options, OptionsImpl};
use packet::{
    AsFragmentedByteSlice, BufferView, FragmentedByteSlice, FromRaw, PacketBuilder,
    PacketConstraints, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::byteorder::{network_endian::U16, ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{IpProtoExt, Ipv4Proto, Ipv6Proto};
use crate::ipv4::{self, Ipv4PacketRaw};
use crate::ipv6::Ipv6PacketRaw;

#[derive(Copy, Clone, Default, Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct HeaderPrefix {
    msg_type: u8,
    code: u8,
    checksum: [u8; 2],
    /* NOTE: The "Rest of Header" field is stored in message types rather than
     * in the HeaderPrefix. This helps consolidate how callers access data about the
     * packet, and is consistent with ICMPv6, which treats the field as part of
     * messages rather than the header. */
}

impl HeaderPrefix {
    fn set_msg_type<T: Into<u8>>(&mut self, msg_type: T) {
        self.msg_type = msg_type.into();
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
pub fn peek_message_type<MessageType: TryFrom<u8>>(bytes: &[u8]) -> ParseResult<MessageType> {
    let (hdr_pfx, _) = LayoutVerified::<_, HeaderPrefix>::new_unaligned_from_prefix(bytes)
        .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
    MessageType::try_from(hdr_pfx.msg_type).map_err(|_| {
        debug_err!(ParseError::NotSupported, "unrecognized message type: {:x}", hdr_pfx.msg_type,)
    })
}

/// An extension trait adding ICMP-related functionality to `Ipv4` and `Ipv6`.
pub trait IcmpIpExt: IpProtoExt {
    /// The type of ICMP messages.
    ///
    /// For `Ipv4`, this is `Icmpv4MessageType`, and for `Ipv6`, this is
    /// `Icmpv6MessageType`.
    type IcmpMessageType: IcmpMessageType;

    /// The type of an ICMP parameter problem code.
    ///
    /// For `Ipv4`, this is `Icmpv4ParameterProblemCode`, and for `Ipv6` this
    /// is `Icmpv6ParameterProblemCode`.
    type ParameterProblemCode: PartialEq + Send + Sync + Debug;

    /// The type of an ICMP parameter problem pointer.
    ///
    /// For `Ipv4`, this is `u8`, and for `Ipv6` this is `u32`.
    type ParameterProblemPointer: PartialEq + Send + Sync + Debug;

    /// The type of an ICMP parameter header length.
    ///
    /// For `Ipv4`, this is `usize`, and for `Ipv6` this is `()`.
    type HeaderLen: PartialEq + Send + Sync + Debug;

    /// The identifier for this ICMP version.
    ///
    /// This value will be found in an IPv4 packet's Protocol field (for ICMPv4
    /// packets) or an IPv6 fixed header's or last extension header's Next
    /// Heeader field (for ICMPv6 packets).
    const ICMP_IP_PROTO: <Self as IpProtoExt>::Proto;

    /// Computes the length of the header of the packet prefix stored in
    /// `bytes`.
    ///
    /// Given the prefix of a packet stored in `bytes`, compute the length of
    /// the header of that packet, or `bytes.len()` if `bytes` does not contain
    /// the entire header. If the version is IPv6, the returned length should
    /// include all extension headers.
    fn header_len(bytes: &[u8]) -> usize;
}

impl IcmpIpExt for Ipv4 {
    type IcmpMessageType = Icmpv4MessageType;
    type ParameterProblemCode = Icmpv4ParameterProblemCode;
    type ParameterProblemPointer = u8;
    type HeaderLen = usize;

    const ICMP_IP_PROTO: Ipv4Proto = Ipv4Proto::Icmp;

    fn header_len(bytes: &[u8]) -> usize {
        if bytes.len() < ipv4::IPV4_MIN_HDR_LEN {
            return bytes.len();
        }
        let (header_prefix, _) =
            LayoutVerified::<_, ipv4::HeaderPrefix>::new_unaligned_from_prefix(bytes).unwrap();
        cmp::min(header_prefix.ihl() as usize * 4, bytes.len())
    }
}

impl IcmpIpExt for Ipv6 {
    type IcmpMessageType = Icmpv6MessageType;
    type ParameterProblemCode = Icmpv6ParameterProblemCode;
    type ParameterProblemPointer = u32;
    type HeaderLen = ();

    const ICMP_IP_PROTO: Ipv6Proto = Ipv6Proto::Icmpv6;

    // TODO: Re-implement this in terms of partial parsing, and then get rid of
    // the `header_len` method.
    fn header_len(_bytes: &[u8]) -> usize {
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
pub trait IcmpPacketType<B: ByteSlice, I: Ip>:
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
pub trait MessageBody<B>: Sized {
    /// Whether or not a message body is expected in an ICMP packet.
    const EXPECTS_BODY: bool = true;

    /// Parse the MessageBody from the provided bytes.
    fn parse(bytes: B) -> ParseResult<Self>
    where
        B: ByteSlice;

    /// The length of the underlying buffer.
    fn len(&self) -> usize
    where
        B: ByteSlice;

    /// Is the body empty?
    ///
    /// `b.is_empty()` is equivalent to `b.len() == 0`.
    fn is_empty(&self) -> bool
    where
        B: ByteSlice,
    {
        self.len() == 0
    }

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
pub struct OriginalPacket<B>(B);

impl<B: ByteSlice + Deref<Target = [u8]>> OriginalPacket<B> {
    /// Returns the the body of the original packet.
    pub fn body<I: IcmpIpExt>(&self) -> &[u8] {
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
        Self::parse(bytes).map_err(|_e| debug_err!(ParseError::Format, "unable to parse options"))
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
pub trait IcmpMessage<I: IcmpIpExt, B: ByteSlice>:
    Sized + Copy + FromBytes + AsBytes + Unaligned
{
    /// The type of codes used with this message.
    ///
    /// The ICMP header includes an 8-bit "code" field. For a given message
    /// type, different values of this field carry different meanings. Not all
    /// code values are used - some may be invalid. This type represents a
    /// parsed code. For example, for TODO, it is the TODO type.
    type Code: Into<u8> + Copy + Debug;

    /// The type of the body used with this message.
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
pub trait IcmpMessageType: TryFrom<u8> + Into<u8> + Copy {
    /// Is this an error message?
    ///
    /// For ICMP, this is true for the Destination Unreachable, Redirect, Source
    /// Quench, Time Exceeded, and Parameter Problem message types. For ICMPv6,
    /// this is true for the Destination Unreachable, Packet Too Big, Time
    /// Exceeded, and Parameter Problem message types.
    fn is_err(self) -> bool;
}

#[derive(Copy, Clone, Debug, FromBytes, Unaligned)]
#[repr(C)]
struct Header<M> {
    prefix: HeaderPrefix,
    message: M,
}

// So long as `M: Unaligned`, there will be no padding between the
// `HeaderPrefix` and `M`. Since `HeaderPrefix` itself is `Unaligned`, the
// alignment of `Header<M>` will be 1, meaning that no post-padding will need to
// be added to get to a multiple of the alignment. Since there is no padding,
// then so long as `M: AsBytes`, all of `Header<M>: AsBytes`.
unsafe impl<M: AsBytes + Unaligned> AsBytes for Header<M> {
    // We're doing a bad thing, but it's necessary until derive(AsBytes)
    // supports type parameters.
    fn only_derive_is_allowed_to_implement_this_trait() {}
}

/// A partially parsed and not yet validated ICMP packet.
///
/// An `IcmpPacketRaw` provides minimal parsing of an ICMP packet. Namely, it
/// only requires that the header and message (in ICMPv6, these are both
/// considered part of the header) are present, and that the header has the
/// expected message type. The body may be missing (or an unexpected body may be
/// present). Other than the message type, no header, message, or body field
/// values will be validated.
///
/// [`IcmpPacket`] provides a [`FromRaw`] implementation that can be used to
/// validate an [`IcmpPacketRaw`].
#[derive(Debug)]
pub struct IcmpPacketRaw<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> {
    header: LayoutVerified<B, Header<M>>,
    message_body: B,
    _marker: PhantomData<I>,
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacketRaw<I, B, M> {
    /// Get the ICMP message.
    pub fn message(&self) -> &M {
        &self.header.message
    }
}

/// An ICMP packet.
///
/// An `IcmpPacket` shares its underlying memory with the byte slice it was
/// parsed from, meaning that no copying or extra allocation is necessary.
#[derive(Debug)]
pub struct IcmpPacket<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> {
    header: LayoutVerified<B, Header<M>>,
    message_body: M::Body,
    _marker: PhantomData<I>,
}

/// Arguments required to parse an ICMP packet.
pub struct IcmpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> IcmpParseArgs<A> {
    /// Construct a new `IcmpParseArgs`.
    pub fn new<S: Into<A>, D: Into<A>>(src_ip: S, dst_ip: D) -> IcmpParseArgs<A> {
        IcmpParseArgs { src_ip: src_ip.into(), dst_ip: dst_ip.into() }
    }
}

impl<B: ByteSlice, I: IcmpIpExt, M: IcmpMessage<I, B>> ParsablePacket<B, ()>
    for IcmpPacketRaw<I, B, M>
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.message_body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> ParseResult<Self> {
        let header = buffer
            .take_obj_front::<Header<M>>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let message_body = buffer.into_rest();
        if header.prefix.msg_type != M::TYPE.into() {
            return debug_err!(Err(ParseError::NotExpected), "unexpected message type");
        }
        Ok(IcmpPacketRaw { header, message_body, _marker: PhantomData })
    }
}

impl<B: ByteSlice, I: IcmpIpExt, M: IcmpMessage<I, B>>
    FromRaw<IcmpPacketRaw<I, B, M>, IcmpParseArgs<I::Addr>> for IcmpPacket<I, B, M>
{
    type Error = ParseError;

    fn try_from_raw_with(
        raw: IcmpPacketRaw<I, B, M>,
        args: IcmpParseArgs<I::Addr>,
    ) -> ParseResult<Self> {
        let IcmpPacketRaw { header, message_body, _marker } = raw;
        if !M::Body::EXPECTS_BODY && !message_body.is_empty() {
            return debug_err!(Err(ParseError::Format), "unexpected message body");
        }
        let _: M::Code = M::code_from_u8(header.prefix.code).ok_or_else(debug_err_fn!(
            ParseError::Format,
            "unrecognized code: {}",
            header.prefix.code
        ))?;
        let checksum = Self::compute_checksum(&header, &message_body, args.src_ip, args.dst_ip)
            .ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?;
        if checksum != [0, 0] {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }
        let message_body = M::Body::parse(message_body)?;
        Ok(IcmpPacket { header, message_body, _marker })
    }
}

impl<B: ByteSlice, I: IcmpIpExt, M: IcmpMessage<I, B>> ParsablePacket<B, IcmpParseArgs<I::Addr>>
    for IcmpPacket<I, B, M>
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.message_body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: IcmpParseArgs<I::Addr>) -> ParseResult<Self> {
        IcmpPacketRaw::parse(buffer, ()).and_then(|p| IcmpPacket::try_from_raw_with(p, args))
    }
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacket<I, B, M> {
    /// Get the ICMP message.
    pub fn message(&self) -> &M {
        &self.header.message
    }

    /// Get the ICMP body.
    pub fn body(&self) -> &M::Body {
        &self.message_body
    }

    /// Get the ICMP message code.
    ///
    /// The code provides extra details about the message. Each message type has
    /// its own set of codes that are allowed.
    pub fn code(&self) -> M::Code {
        // infallible since it was validated in parse
        M::code_from_u8(self.header.prefix.code).unwrap()
    }

    /// Construct a builder with the same contents as this packet.
    pub fn builder(&self, src_ip: I::Addr, dst_ip: I::Addr) -> IcmpPacketBuilder<I, B, M> {
        IcmpPacketBuilder { src_ip, dst_ip, code: self.code(), msg: *self.message() }
    }
}

fn compute_checksum_fragmented<
    I: IcmpIpExt,
    B: ByteSlice,
    BB: packet::Fragment,
    M: IcmpMessage<I, B>,
>(
    header: &Header<M>,
    message_body: &FragmentedByteSlice<'_, BB>,
    src_ip: I::Addr,
    dst_ip: I::Addr,
) -> Option<[u8; 2]> {
    let mut c = Checksum::new();
    if I::VERSION.is_v6() {
        c.add_bytes(src_ip.bytes());
        c.add_bytes(dst_ip.bytes());
        let icmpv6_len = mem::size_of::<Header<M>>() + message_body.len();
        let mut len_bytes = [0; 4];
        NetworkEndian::write_u32(&mut len_bytes, icmpv6_len.try_into().ok()?);
        c.add_bytes(&len_bytes[..]);
        c.add_bytes(&[0, 0, 0]);
        c.add_bytes(&[Ipv6Proto::Icmpv6.into()]);
    }
    c.add_bytes(&[header.prefix.msg_type, header.prefix.code]);
    c.add_bytes(&header.prefix.checksum);
    c.add_bytes(header.message.as_bytes());
    for p in message_body.iter_fragments() {
        c.add_bytes(p);
    }
    Some(c.checksum())
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacket<I, B, M> {
    /// Compute the checksum, including the checksum field itself.
    ///
    /// `compute_checksum` returns `None` if the version is IPv6 and the total
    /// ICMP packet length overflows a u32.
    fn compute_checksum(
        header: &Header<M>,
        message_body: &[u8],
        src_ip: I::Addr,
        dst_ip: I::Addr,
    ) -> Option<[u8; 2]> {
        let mut body = [message_body];
        compute_checksum_fragmented(header, &body.as_fragmented_byte_slice(), src_ip, dst_ip)
    }
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B, Body = OriginalPacket<B>>>
    IcmpPacket<I, B, M>
{
    /// Get the body of the packet that caused this ICMP message.
    ///
    /// This ICMP message contains some of the bytes of the packet that caused
    /// this message to be emitted. `original_packet_body` returns as much of
    /// the body of that packet as is contained in this message. For IPv4, this
    /// is guaranteed to be 8 bytes. For IPv6, there are no guarantees about the
    /// length.
    pub fn original_packet_body(&self) -> &[u8] {
        self.message_body.body::<I>()
    }

    /// Returns the original packt that caused this ICMP message.
    ///
    /// This ICMP message contains some of the bytes of the packet that caused
    /// this message to be emitted. `original_packet` returns as much of the
    /// body of that packet as is contained in this message. For IPv4, this is
    /// guaranteed to be 8 bytes. For IPv6, there are no guarantees about the
    /// length.
    pub fn original_packet(&self) -> &OriginalPacket<B> {
        &self.message_body
    }
}

impl<B: ByteSlice, M: IcmpMessage<Ipv4, B, Body = OriginalPacket<B>>> IcmpPacket<Ipv4, B, M> {
    /// Attempt to partially parse the original packet as an IPv4 packet.
    ///
    /// `f` will be invoked on the result of calling `Ipv4PacketRaw::parse` on
    /// the original packet.
    pub fn with_original_packet<O, F: FnOnce(Result<Ipv4PacketRaw<&[u8]>, &[u8]>) -> O>(
        &self,
        f: F,
    ) -> O {
        let mut bv = self.message_body.0.deref();
        f(Ipv4PacketRaw::parse(&mut bv, ()).map_err(|_| self.message_body.0.deref()))
    }
}

impl<B: ByteSlice, M: IcmpMessage<Ipv6, B, Body = OriginalPacket<B>>> IcmpPacket<Ipv6, B, M> {
    /// Attempt to partially parse the original packet as an IPv6 packet.
    ///
    /// `f` will be invoked on the result of calling `Ipv6PacketRaw::parse` on
    /// the original packet.
    pub fn with_original_packet<O, F: FnOnce(Result<Ipv6PacketRaw<&[u8]>, &[u8]>) -> O>(
        &self,
        f: F,
    ) -> O {
        let mut bv = self.message_body.0.deref();
        f(Ipv6PacketRaw::parse(&mut bv, ()).map_err(|_| self.message_body.0.deref()))
    }
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B, Body = ndp::Options<B>>> IcmpPacket<I, B, M> {
    /// Get the pared list of NDP options from the ICMP message.
    pub fn ndp_options(&self) -> &ndp::Options<B> {
        &self.message_body
    }
}

/// A builder for ICMP packets.
#[derive(Debug)]
pub struct IcmpPacketBuilder<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> {
    src_ip: I::Addr,
    dst_ip: I::Addr,
    code: M::Code,
    msg: M,
}

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> IcmpPacketBuilder<I, B, M> {
    /// Construct a new `IcmpPacketBuilder`.
    pub fn new<S: Into<I::Addr>, D: Into<I::Addr>>(
        src_ip: S,
        dst_ip: D,
        code: M::Code,
        msg: M,
    ) -> IcmpPacketBuilder<I, B, M> {
        IcmpPacketBuilder { src_ip: src_ip.into(), dst_ip: dst_ip.into(), code, msg }
    }
}

// TODO(joshlf): Figure out a way to split body and non-body message types by
// trait and implement PacketBuilder for some and InnerPacketBuilder for others.

impl<I: IcmpIpExt, B: ByteSlice, M: IcmpMessage<I, B>> PacketBuilder
    for IcmpPacketBuilder<I, B, M>
{
    fn constraints(&self) -> PacketConstraints {
        // The maximum body length constraint to make sure the body length
        // doesn't overflow the 32-bit length field in the pseudo-header used
        // for calculating the checksum.
        //
        // Note that, for messages that don't take bodies, it's important that
        // we don't just set this to 0. Trying to serialize a body in a message
        // type which doesn't take bodies is a programmer error, so we should
        // panic in that case. Setting the max_body_len to 0 would surface the
        // issue as an MTU error, which would hide the underlying problem.
        // Instead, we assert in serialize. Eventually, we will hopefully figure
        // out a way to implement InnerPacketBuilder (rather than PacketBuilder)
        // for these message types, and this won't be an issue anymore.
        PacketConstraints::new(mem::size_of::<Header<M>>(), 0, 0, core::u32::MAX as usize)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
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
            prefix.take_obj_front_zero::<Header<M>>().expect("too few bytes for ICMP message");
        header.prefix.set_msg_type(M::TYPE);
        header.prefix.code = self.code.into();
        header.message = self.msg;
        let checksum = compute_checksum_fragmented(&header, message_body, self.src_ip, self.dst_ip)
            .unwrap_or_else(|| {
                panic!(
                    "total ICMP packet length of {} overflows 32-bit length field of pseudo-header",
                    header.bytes().len() + message_body.len(),
                )
            });
        header.prefix.checksum = checksum;
    }
}

/// The type of ICMP codes that are unused.
///
/// Some ICMP messages do not use codes. In Rust, the `IcmpMessage::Code` type
/// associated with these messages is `IcmpUnusedCode`. The only valid numerical
/// value for this code is 0.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct IcmpUnusedCode;

impl From<IcmpUnusedCode> for u8 {
    fn from(_: IcmpUnusedCode) -> u8 {
        0
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct IdAndSeq {
    id: U16,
    seq: U16,
}

impl IdAndSeq {
    fn new(id: u16, seq: u16) -> IdAndSeq {
        IdAndSeq { id: U16::new(id), seq: U16::new(seq) }
    }
}

#[cfg(test)]
mod tests {
    use packet::ParseBuffer;

    use super::*;

    #[test]
    fn test_partial_parse() {
        // Test various behaviors of parsing the `IcmpPacketRaw` type.

        let reference_header = Header {
            prefix: HeaderPrefix {
                msg_type: <IcmpEchoRequest as IcmpMessage<Ipv4, &[u8]>>::TYPE.into(),
                code: 0,
                checksum: [0, 0],
            },
            message: IcmpEchoRequest::new(1, 1),
        };

        // Test that a too-short header is always rejected even if its contents
        // are otherwise valid (the checksum here is probably invalid, but we
        // explicitly check that it's a `Format` error, not a `Checksum`
        // error).
        let mut buf = &reference_header.as_bytes()[..7];
        assert_eq!(
            buf.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>().unwrap_err(),
            ParseError::Format
        );

        // Test that a properly-sized header is rejected if the message type is wrong.
        let mut header = reference_header;
        header.prefix.msg_type = <IcmpEchoReply as IcmpMessage<Ipv4, &[u8]>>::TYPE.into();
        let mut buf = header.as_bytes();
        assert_eq!(
            buf.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>().unwrap_err(),
            ParseError::NotExpected
        );

        // Test that an invalid code is accepted.
        let mut header = reference_header;
        header.prefix.code = 0xFF;
        let mut buf = header.as_bytes();
        assert!(buf.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>().is_ok());

        // Test that an invalid checksum is accepted. Instead of calculating the
        // correct checksum, we just provide two different checksums. They can't
        // both be valid.
        let mut buf = reference_header.as_bytes();
        assert!(buf.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>().is_ok());
        let mut header = reference_header;
        header.prefix.checksum = [1, 1];
        let mut buf = header.as_bytes();
        assert!(buf.parse::<IcmpPacketRaw<Ipv4, _, IcmpEchoRequest>>().is_ok());
    }
}
