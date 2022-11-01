// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of UDP packets.
//!
//! The UDP packet format is defined in [RFC 768].
//!
//! [RFC 768]: https://datatracker.ietf.org/doc/html/rfc768

use core::convert::TryInto;
use core::fmt::Debug;
#[cfg(test)]
use core::fmt::{self, Formatter};
use core::num::NonZeroU16;
use core::ops::Range;

use net_types::ip::{Ip, IpAddress, IpVersionMarker};
use packet::{
    BufferView, BufferViewMut, ByteSliceInnerPacketBuilder, EmptyBuf, FromRaw, InnerPacketBuilder,
    MaybeParsed, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata, SerializeBuffer,
    Serializer,
};
use zerocopy::{
    byteorder::network_endian::U16, AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned,
};

use crate::error::{ParseError, ParseResult};
use crate::ip::IpProto;
use crate::{compute_transport_checksum_parts, compute_transport_checksum_serialize};

pub(crate) const HEADER_BYTES: usize = 8;
const CHECKSUM_OFFSET: usize = 6;
const CHECKSUM_RANGE: Range<usize> = CHECKSUM_OFFSET..CHECKSUM_OFFSET + 2;

#[derive(Debug, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct Header {
    src_port: U16,
    dst_port: U16,
    length: U16,
    checksum: [u8; 2],
}

/// A UDP packet.
///
/// A `UdpPacket` shares its underlying memory with the byte slice it was parsed
/// from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// A `UdpPacket` - whether parsed using `parse` or created using `serialize` -
/// maintains the invariant that the checksum is always valid.
pub struct UdpPacket<B> {
    header: LayoutVerified<B, Header>,
    body: B,
}

/// Arguments required to parse a UDP packet.
pub struct UdpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> UdpParseArgs<A> {
    /// Construct a new `UdpParseArgs`.
    pub fn new(src_ip: A, dst_ip: A) -> UdpParseArgs<A> {
        UdpParseArgs { src_ip, dst_ip }
    }
}

impl<B: ByteSlice, A: IpAddress> FromRaw<UdpPacketRaw<B>, UdpParseArgs<A>> for UdpPacket<B> {
    type Error = ParseError;

    fn try_from_raw_with(raw: UdpPacketRaw<B>, args: UdpParseArgs<A>) -> Result<Self, Self::Error> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure
        let header = raw
            .header
            .ok_or_else(|_| debug_err!(ParseError::Format, "too few bytes for header"))?;
        let body = raw.body.ok_or_else(|_| debug_err!(ParseError::Format, "incomplete body"))?;

        let checksum = header.checksum;
        // A 0 checksum indicates that the checksum wasn't computed. In IPv4,
        // this means that it shouldn't be validated. In IPv6, the checksum is
        // mandatory, so this is an error.
        if checksum != [0, 0] {
            let parts = [header.bytes(), body.deref().as_ref()];
            let checksum = compute_transport_checksum_parts(
                args.src_ip,
                args.dst_ip,
                IpProto::Udp.into(),
                parts.iter(),
            )
            .ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?;

            // Even the checksum is transmitted as 0xFFFF, the checksum of the whole
            // UDP packet should still be 0. This is because in 1's complement, it is
            // not possible to produce +0(0) from adding non-zero 16-bit words.
            // Since our 0xFFFF ensures there is at least one non-zero 16-bit word,
            // the addition can only produce -0(0xFFFF) and after negation, it is
            // still 0. A test `test_udp_checksum_0xffff` is included to make sure
            // this is true.
            if checksum != [0, 0] {
                return debug_err!(
                    Err(ParseError::Checksum),
                    "invalid checksum {:X?}",
                    header.checksum,
                );
            }
        } else if A::Version::VERSION.is_v6() {
            return debug_err!(Err(ParseError::Format), "missing checksum");
        }

        if header.dst_port.get() == 0 {
            return debug_err!(Err(ParseError::Format), "zero destination port");
        }

        Ok(UdpPacket { header, body })
    }
}

impl<B: ByteSlice, A: IpAddress> ParsablePacket<B, UdpParseArgs<A>> for UdpPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: UdpParseArgs<A>) -> ParseResult<Self> {
        UdpPacketRaw::<B>::parse(buffer, IpVersionMarker::<A::Version>::default())
            .and_then(|u| UdpPacket::try_from_raw_with(u, args))
    }
}

impl<B: ByteSlice> UdpPacket<B> {
    /// The packet body.
    pub fn body(&self) -> &[u8] {
        self.body.deref()
    }

    /// Consumes this packet and returns the body.
    ///
    /// Note that the returned `B` has the same lifetime as the buffer from
    /// which this packet was parsed. By contrast, the [`body`] method returns a
    /// slice with the same lifetime as the receiver.
    ///
    /// [`body`]: UdpPacket::body
    pub fn into_body(self) -> B {
        self.body
    }

    /// The source UDP port, if any.
    ///
    /// The source port is optional, and may have been omitted by the sender.
    pub fn src_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(self.header.src_port.get())
    }

    /// The destination UDP port.
    pub fn dst_port(&self) -> NonZeroU16 {
        // Infallible because it was validated in parse.
        NonZeroU16::new(self.header.dst_port.get()).unwrap()
    }

    /// Did this packet have a checksum?
    ///
    /// On IPv4, the sender may optionally omit the checksum. If this function
    /// returns false, the sender omitted the checksum, and `parse` will not
    /// have validated it.
    ///
    /// On IPv6, it is guaranteed that `checksummed` will return true because
    /// IPv6 requires a checksum, and so any UDP packet missing one will fail
    /// validation in `parse`.
    pub fn checksummed(&self) -> bool {
        self.header.checksum != U16::ZERO
    }

    /// Constructs a builder with the same contents as this packet.
    pub fn builder<A: IpAddress>(&self, src_ip: A, dst_ip: A) -> UdpPacketBuilder<A> {
        UdpPacketBuilder {
            src_ip,
            dst_ip,
            src_port: self.src_port(),
            dst_port: Some(self.dst_port()),
        }
    }

    /// Consumes this packet and constructs a [`Serializer`] with the same
    /// contents.
    ///
    /// The returned `Serializer` has the [`Buffer`] type [`EmptyBuf`], which
    /// means it is not able to reuse the buffer backing this `UdpPacket` when
    /// serializing, and will always need to allocate a new buffer.
    ///
    /// By consuming `self` instead of taking it by-reference, `into_serializer`
    /// is able to return a `Serializer` whose lifetime is restricted by the
    /// lifetime of the buffer from which this `UdpPacket` was parsed rather
    /// than by the lifetime on `&self`, which may be more restricted.
    ///
    /// [`Buffer`]: packet::Serializer::Buffer
    pub fn into_serializer<'a, A: IpAddress>(
        self,
        src_ip: A,
        dst_ip: A,
    ) -> impl Serializer<Buffer = EmptyBuf> + Debug + 'a
    where
        B: 'a,
    {
        let builder = self.builder(src_ip, dst_ip);
        ByteSliceInnerPacketBuilder(self.body).into_serializer().encapsulate(builder)
    }
}

/// The minimal information required from a UDP packet header.
///
/// A `UdpPacketHeader` may be the result of a partially parsed UDP packet in
/// [`UdpPacketRaw`].
#[derive(Debug, Default, FromBytes, AsBytes, Unaligned, PartialEq)]
#[repr(C)]
struct UdpFlowHeader {
    src_port: U16,
    dst_port: U16,
}

/// A partially parsed UDP packet header.
#[derive(Debug)]
struct PartialHeader<B: ByteSlice> {
    flow: LayoutVerified<B, UdpFlowHeader>,
    rest: B,
}

/// A partially-parsed and not yet validated UDP packet.
///
/// A `UdpPacketRaw` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// Parsing a `UdpPacketRaw` from raw data will succeed as long as at least 4
/// bytes are available, which will be extracted as a [`UdpFlowHeader`] that
/// contains the UDP source and destination ports. A `UdpPacketRaw` is, then,
/// guaranteed to always have at least that minimal information available.
///
/// [`UdpPacket`] provides a [`FromRaw`] implementation that can be used to
/// validate a `UdpPacketRaw`.
pub struct UdpPacketRaw<B: ByteSlice> {
    header: MaybeParsed<LayoutVerified<B, Header>, PartialHeader<B>>,
    body: MaybeParsed<B, B>,
}

impl<B, I> ParsablePacket<B, IpVersionMarker<I>> for UdpPacketRaw<B>
where
    B: ByteSlice,
    I: Ip,
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = match &self.header {
            MaybeParsed::Complete(h) => h.bytes().len(),
            MaybeParsed::Incomplete(h) => h.flow.bytes().len() + h.rest.len(),
        };
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: IpVersionMarker<I>) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let header = if let Some(header) = buffer.take_obj_front::<Header>() {
            header
        } else {
            let flow = buffer
                .take_obj_front::<UdpFlowHeader>()
                .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for flow header"))?;
            // if we can't parse an entire header, just return early since
            // there's no way to look into how many body bytes to consume:
            return Ok(UdpPacketRaw {
                header: MaybeParsed::Incomplete(PartialHeader {
                    flow,
                    rest: buffer.take_rest_front(),
                }),
                body: MaybeParsed::Incomplete(buffer.into_rest()),
            });
        };
        let buffer_len = buffer.len();

        fn get_udp_body_length<I: Ip>(header: &Header, remaining_buff_len: usize) -> Option<usize> {
            // IPv6 supports jumbograms, so a UDP packet may be greater than
            // 2^16 bytes in size. In this case, the size doesn't fit in the
            // 16-bit length field in the header, and so the length field is set
            // to zero to indicate this.
            //
            // Per RFC 2675 Section 4, we only do that if the UDP header plus
            // data is actually more than 65535.
            if I::VERSION.is_v6()
                && header.length.get() == 0
                && remaining_buff_len.saturating_add(HEADER_BYTES) >= (core::u16::MAX as usize)
            {
                return Some(remaining_buff_len);
            }

            usize::from(header.length.get()).checked_sub(HEADER_BYTES)
        }

        let body = if let Some(body_len) = get_udp_body_length::<I>(&header, buffer_len) {
            if body_len <= buffer_len {
                // Discard any padding left by the previous layer. The unwrap is safe
                // and the subtraction is always valid because body_len is guaranteed
                // to not exceed buffer.len()
                let _: B = buffer.take_back(buffer_len - body_len).unwrap();
                MaybeParsed::Complete(buffer.into_rest())
            } else {
                // buffer does not contain all the body bytes
                MaybeParsed::Incomplete(buffer.into_rest())
            }
        } else {
            // body_len can't be calculated because it's less than the header
            // length, consider all the rest of the buffer padding and return
            // an incomplete empty body.
            let _: B = buffer.take_rest_back();
            MaybeParsed::Incomplete(buffer.into_rest())
        };

        Ok(UdpPacketRaw { header: MaybeParsed::Complete(header), body })
    }
}

impl<B: ByteSlice> UdpPacketRaw<B> {
    /// The source UDP port, if any.
    ///
    /// The source port is optional, and may have been omitted by the sender.
    pub fn src_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(
            self.header
                .as_ref()
                .map(|header| header.src_port)
                .map_incomplete(|partial_header| partial_header.flow.src_port)
                .into_inner()
                .get(),
        )
    }

    /// The destination UDP port.
    ///
    /// UDP packets must not have a destination port of 0; thus, if this
    /// function returns `None`, then the packet is malformed.
    pub fn dst_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(
            self.header
                .as_ref()
                .map(|header| header.dst_port)
                .map_incomplete(|partial_header| partial_header.flow.dst_port)
                .into_inner()
                .get(),
        )
    }

    /// Constructs a builder with the same contents as this packet.
    ///
    /// Note that, since `UdpPacketRaw` does not validate its header fields,
    /// it's possible for `builder` to produce a `UdpPacketBuilder` which
    /// describes an invalid UDP packet. In particular, it's possible that its
    /// destination port will be zero, which is illegal.
    pub fn builder<A: IpAddress>(&self, src_ip: A, dst_ip: A) -> UdpPacketBuilder<A> {
        UdpPacketBuilder { src_ip, dst_ip, src_port: self.src_port(), dst_port: self.dst_port() }
    }

    /// Consumes this packet and constructs a [`Serializer`] with the same
    /// contents.
    ///
    /// Returns `None` if the body was not fully parsed.
    ///
    /// This method has the same validity caveats as [`builder`].
    ///
    /// The returned `Serializer` has the [`Buffer`] type [`EmptyBuf`], which
    /// means it is not able to reuse the buffer backing this `UdpPacket` when
    /// serializing, and will always need to allocate a new buffer.
    ///
    /// By consuming `self` instead of taking it by-reference, `into_serializer`
    /// is able to return a `Serializer` whose lifetime is restricted by the
    /// lifetime of the buffer from which this `UdpPacket` was parsed rather
    /// than by the lifetime on `&self`, which may be more restricted.
    ///
    /// [`builder`]: UdpPacketRaw::builder
    /// [`Buffer`]: packet::Serializer::Buffer
    pub fn into_serializer<'a, A: IpAddress>(
        self,
        src_ip: A,
        dst_ip: A,
    ) -> Option<impl Serializer<Buffer = EmptyBuf> + 'a>
    where
        B: 'a,
    {
        let builder = self.builder(src_ip, dst_ip);
        self.body
            .complete()
            .ok()
            .map(|body| ByteSliceInnerPacketBuilder(body).into_serializer().encapsulate(builder))
    }
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the UDP packet; the only way to set them
// is via UdpPacketBuilder::serialize. This, combined with checksum validation
// performed in UdpPacket::parse, provides the invariant that a UdpPacket always
// has a valid checksum.

/// A builder for UDP packets.
#[derive(Copy, Clone, Debug)]
pub struct UdpPacketBuilder<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
    src_port: Option<NonZeroU16>,
    dst_port: Option<NonZeroU16>,
}

impl<A: IpAddress> UdpPacketBuilder<A> {
    /// Constructs a new `UdpPacketBuilder`.
    pub fn new(
        src_ip: A,
        dst_ip: A,
        src_port: Option<NonZeroU16>,
        dst_port: NonZeroU16,
    ) -> UdpPacketBuilder<A> {
        UdpPacketBuilder { src_ip, dst_ip, src_port, dst_port: Some(dst_port) }
    }
}

impl<A: IpAddress> PacketBuilder for UdpPacketBuilder<A> {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            HEADER_BYTES,
            0,
            0,
            if A::Version::VERSION.is_v4() {
                (1 << 16) - 1
            } else {
                // IPv6 supports jumbograms, so a UDP packet may be greater than
                // 2^16 bytes. In this case, the size doesn't fit in the 16-bit
                // length field in the header, and so the length field is set to
                // zero. That means that, from this packet's perspective,
                // there's no effective limit on the body size.
                core::usize::MAX
            },
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let total_len = buffer.len();
        let mut header = buffer.header();
        // implements BufferViewMut, giving us write_obj_front method
        let mut header = &mut header;

        header.write_obj_front(&Header {
            src_port: U16::new(self.src_port.map_or(0, NonZeroU16::get)),
            dst_port: U16::new(self.dst_port.map_or(0, NonZeroU16::get)),
            length: U16::new(total_len.try_into().unwrap_or_else(|_| {
                if A::Version::VERSION.is_v6() {
                    // See comment in max_body_len
                    0u16
                } else {
                    panic!(
                    "total UDP packet length of {} bytes overflows 16-bit length field of UDP header",
                    total_len)
                }
            })),
            // Initialize the checksum to 0 so that we will get the correct
            // value when we compute it below.
            checksum: [0, 0],
        }).expect("too few bytes for UDP header");

        let mut checksum = compute_transport_checksum_serialize(
            self.src_ip,
            self.dst_ip,
            IpProto::Udp.into(),
            buffer,
        )
        .unwrap_or_else(|| {
            panic!(
                "total UDP packet length of {} bytes overflows length field of pseudo-header",
                total_len
            )
        });
        if checksum == [0, 0] {
            checksum = [0xFF, 0xFF];
        }
        buffer.header()[CHECKSUM_RANGE].copy_from_slice(&checksum[..]);
    }
}

// needed by Result::unwrap_err in the tests below
#[cfg(test)]
impl<B> Debug for UdpPacket<B> {
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        write!(fmt, "UdpPacket")
    }
}

#[cfg(test)]
mod tests {
    use core::num::NonZeroU16;
    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr};
    use packet::{Buf, FragmentedBytesMut, InnerPacketBuilder, ParseBuffer, Serializer};
    use zerocopy::byteorder::{ByteOrder, NetworkEndian};

    use super::*;
    use crate::ethernet::{EthernetFrame, EthernetFrameLengthCheck};
    use crate::ipv4::{Ipv4Header, Ipv4Packet};
    use crate::ipv6::{Ipv6Header, Ipv6Packet};
    use crate::testutil::benchmarks::{black_box, Bencher};
    use crate::testutil::*;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr =
        Ipv6Addr::from_bytes([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    #[test]
    fn test_parse_serialize_full_ipv4() {
        use crate::testdata::dns_request_v4::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let ip_packet = body.parse::<Ipv4Packet<_>>().unwrap();
        verify_ipv4_packet(&ip_packet, IPV4_PACKET);

        let mut body = ip_packet.body();
        let udp_packet = body
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(
                ip_packet.src_ip(),
                ip_packet.dst_ip(),
            ))
            .unwrap();
        verify_udp_packet(&udp_packet, UDP_PACKET);

        let buffer = udp_packet
            .body()
            .into_serializer()
            .encapsulate(udp_packet.builder(ip_packet.src_ip(), ip_packet.dst_ip()))
            .encapsulate(ip_packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);
    }

    #[test]
    fn test_parse_serialize_full_ipv6() {
        use crate::testdata::dns_request_v6::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let ip_packet = body.parse::<Ipv6Packet<_>>().unwrap();
        verify_ipv6_packet(&ip_packet, IPV6_PACKET);

        let mut body = ip_packet.body();
        let udp_packet = body
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(
                ip_packet.src_ip(),
                ip_packet.dst_ip(),
            ))
            .unwrap();
        verify_udp_packet(&udp_packet, UDP_PACKET);

        let buffer = udp_packet
            .body()
            .into_serializer()
            .encapsulate(udp_packet.builder(ip_packet.src_ip(), ip_packet.dst_ip()))
            .encapsulate(ip_packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);
    }

    #[test]
    fn test_parse() {
        // source port of 0 (meaning none) is allowed, as is a missing checksum
        let mut buf = &[0, 0, 1, 2, 0, 8, 0, 0][..];
        let packet = buf
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        assert!(packet.src_port().is_none());
        assert_eq!(packet.dst_port().get(), NetworkEndian::read_u16(&[1, 2]));
        assert!(!packet.checksummed());
        assert!(packet.body().is_empty());

        // length of 0 is allowed in IPv6 if the body is long enough
        let mut buf = vec![0_u8, 0, 1, 2, 0, 0, 0xBF, 0x12];
        buf.extend((0..core::u16::MAX).into_iter().map(|p| p as u8));
        let bv = &mut &buf[..];
        let packet = bv
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(TEST_SRC_IPV6, TEST_DST_IPV6))
            .unwrap();
        assert!(packet.src_port().is_none());
        assert_eq!(packet.dst_port().get(), NetworkEndian::read_u16(&[1, 2]));
        assert!(packet.checksummed());
        assert_eq!(packet.body().len(), core::u16::MAX as usize);
    }

    #[test]
    fn test_serialize() {
        let mut buf = (&[])
            .into_serializer()
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buf.as_ref(), [0, 1, 0, 2, 0, 8, 239, 199]);
        let packet = buf
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(packet.src_port().unwrap().get(), 1);
        assert_eq!(packet.dst_port().get(), 2);
        assert!(packet.checksummed());
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that UdpPacket::serialize properly zeroes memory before serializing
        // the header.
        let mut buf_0 = [0; HEADER_BYTES];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_0[..], HEADER_BYTES..)
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        let mut buf_1 = [0xFF; HEADER_BYTES];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_1[..], HEADER_BYTES..)
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    fn test_parse_error() {
        // Test that while a given byte pattern optionally succeeds, zeroing out
        // certain bytes causes failure. `zero` is a list of byte indices to
        // zero out that should cause failure.
        fn test_zero<I: IpAddress>(
            src: I,
            dst: I,
            succeeds: bool,
            zero: &[usize],
            err: ParseError,
        ) {
            // Set checksum to zero so that, in IPV4, it will be ignored. In
            // IPv6, this /is/ the test.
            let mut buf = [1, 2, 3, 4, 0, 8, 0, 0];
            if succeeds {
                let mut buf = &buf[..];
                assert!(buf.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src, dst)).is_ok());
            }
            for idx in zero {
                buf[*idx] = 0;
            }
            let mut buf = &buf[..];
            assert_eq!(
                buf.parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(src, dst)).unwrap_err(),
                err
            );
        }

        // destination port of 0 is disallowed
        test_zero(TEST_SRC_IPV4, TEST_DST_IPV4, true, &[2, 3], ParseError::Format);
        // length of 0 is disallowed in IPv4
        test_zero(TEST_SRC_IPV4, TEST_DST_IPV4, true, &[4, 5], ParseError::Format);
        // missing checksum is disallowed in IPv6; this won't succeed ahead of
        // time because the checksum bytes are already zero
        test_zero(TEST_SRC_IPV6, TEST_DST_IPV6, false, &[], ParseError::Format);

        // 2^32 overflows on 32-bit platforms
        #[cfg(target_pointer_width = "64")]
        {
            // total length of 2^32 or greater is disallowed in IPv6
            let mut buf = vec![0u8; 1 << 32];
            (&mut buf[..HEADER_BYTES]).copy_from_slice(&[0, 0, 1, 2, 0, 0, 0xFF, 0xE4]);
            assert_eq!(
                (&buf[..])
                    .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(TEST_SRC_IPV6, TEST_DST_IPV6))
                    .unwrap_err(),
                ParseError::Format
            );
        }
    }

    #[test]
    #[should_panic(expected = "too few bytes for UDP header")]
    fn test_serialize_fail_header_too_short() {
        let mut buf = [0u8; 7];
        let mut buf = [&mut buf[..]];
        let buf = FragmentedBytesMut::new(&mut buf[..]);
        let (head, body, foot) = buf.try_split_contiguous(..).unwrap();
        let mut buffer = SerializeBuffer::new(head, body, foot);
        let builder =
            UdpPacketBuilder::new(TEST_SRC_IPV4, TEST_DST_IPV4, None, NonZeroU16::new(1).unwrap());
        builder.serialize(&mut buffer);
    }

    #[test]
    #[should_panic(expected = "total UDP packet length of 65536 bytes overflows 16-bit length \
                               field of UDP header")]
    fn test_serialize_fail_packet_too_long_ipv4() {
        let ser = (&[0; (1 << 16) - HEADER_BYTES][..]).into_serializer().encapsulate(
            UdpPacketBuilder::new(TEST_SRC_IPV4, TEST_DST_IPV4, None, NonZeroU16::new(1).unwrap()),
        );
        let _ = ser.serialize_vec_outer();
    }

    #[test]
    fn test_partial_parse() {
        use core::ops::Deref as _;

        // Try to get something with only the flow header:
        let buf = [0, 0, 1, 2, 10, 20];
        let mut bv = &buf[..];
        let packet =
            bv.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv4>::default()).unwrap();
        let UdpPacketRaw { header, body } = &packet;
        let PartialHeader { flow, rest } = header.as_ref().incomplete().unwrap();
        assert_eq!(
            flow.deref(),
            &UdpFlowHeader { src_port: U16::new(0), dst_port: U16::new(0x0102) }
        );
        assert_eq!(*rest, &buf[4..]);
        assert_eq!(body.incomplete().unwrap(), []);
        assert!(UdpPacket::try_from_raw_with(
            packet,
            UdpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4)
        )
        .is_err());

        // check that we fail if flow header is not retrievable:
        let mut buf = &[0, 0, 1][..];
        assert!(buf.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv4>::default()).is_err());

        // Get an incomplete body:
        let buf = [0, 0, 1, 2, 0, 30, 0, 0, 10, 20];
        let mut bv = &buf[..];
        let packet =
            bv.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv4>::default()).unwrap();
        let UdpPacketRaw { header, body } = &packet;
        assert_eq!(header.as_ref().complete().unwrap().bytes(), &buf[..8]);
        assert_eq!(body.incomplete().unwrap(), &buf[8..]);
        assert!(UdpPacket::try_from_raw_with(
            packet,
            UdpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4)
        )
        .is_err());

        // Incomplete empty body if total length in header is less than 8:
        let buf = [0, 0, 1, 2, 0, 6, 0, 0, 10, 20];
        let mut bv = &buf[..];
        let packet =
            bv.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv4>::default()).unwrap();
        let UdpPacketRaw { header, body } = &packet;
        assert_eq!(header.as_ref().complete().unwrap().bytes(), &buf[..8]);
        assert_eq!(body.incomplete().unwrap(), []);
        assert!(UdpPacket::try_from_raw_with(
            packet,
            UdpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4)
        )
        .is_err());

        // IPv6 allows zero-length body, which will just be the rest of the
        // buffer, but only as long as it has more than 65535 bytes, otherwise
        // it'll just be interpreted as an invalid length:
        let buf = [0, 0, 1, 2, 0, 0, 0, 0, 10, 20];
        let mut bv = &buf[..];
        let packet =
            bv.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv6>::default()).unwrap();
        let UdpPacketRaw { header, body } = &packet;
        assert_eq!(header.as_ref().complete().unwrap().bytes(), &buf[..8]);
        assert_eq!(body.incomplete().unwrap(), []);
        // Now try same thing but with a body that's actually big enough to
        // justify len being 0.
        let mut buf = vec![0, 0, 1, 2, 0, 0, 0, 0, 10, 20];
        buf.extend((0..core::u16::MAX).into_iter().map(|x| x as u8));
        let bv = &mut &buf[..];
        let packet =
            bv.parse_with::<_, UdpPacketRaw<_>>(IpVersionMarker::<Ipv6>::default()).unwrap();
        let UdpPacketRaw { header, body } = &packet;
        assert_eq!(header.as_ref().complete().unwrap().bytes(), &buf[..8]);
        assert_eq!(body.complete().unwrap(), &buf[8..]);
    }

    #[test]
    fn test_udp_checksum_0xffff() {
        // Test the behavior when a UDP packet has to
        // flip its checksum field.
        let builder = (&[0xff, 0xd9]).into_serializer().encapsulate(UdpPacketBuilder::new(
            Ipv4Addr::new([0, 0, 0, 0]),
            Ipv4Addr::new([0, 0, 0, 0]),
            None,
            NonZeroU16::new(1).unwrap(),
        ));
        let buf = builder.serialize_vec_outer().unwrap();
        // The serializer has flipped the bits for us.
        // Normally, 0xFFFF can't be checksum because -0
        // can not be produced by adding non-negtive 16-bit
        // words
        assert_eq!(buf.as_ref()[7], 0xFF);
        assert_eq!(buf.as_ref()[8], 0xFF);

        // When validating the checksum, just add'em up.
        let mut c = internet_checksum::Checksum::new();
        c.add_bytes(&[0, 0, 0, 0, 0, 0, 0, 0, 0, 17, 0, 10]);
        c.add_bytes(buf.as_ref());
        assert!(c.checksum() == [0, 0]);
    }

    // TODO(joshlf): Figure out why compiling this test (yes, just compiling!)
    // hangs the compiler.

    // // This test tries to allocate 4GB of memory. Run at your own risk.
    // #[test]
    // #[should_panic]
    // #[ignore]
    // #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    // fn test_serialize_fail_packet_too_long_ipv6() {
    //     // total length of 2^32 or greater is disallowed in IPv6
    //     let mut buf = vec![0u8; 1 << 32];
    //     (&[0u8; (1 << 32) - HEADER_BYTES])
    //         .encapsulate(UdpPacketBuilder::new(
    //             TEST_SRC_IPV4,
    //             TEST_DST_IPV4,
    //             None,
    //             NonZeroU16::new(1).unwrap(),
    //         ))
    //         .serialize_vec_outer()
    //         .unwrap();
    // }

    //
    // Benchmarks
    //

    fn bench_parse_inner<B: Bencher>(b: &mut B) {
        use crate::testdata::dns_request_v4::*;
        let bytes = parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME.bytes).unwrap().0;

        b.iter(|| {
            let buf = bytes;
            let _: UdpPacket<_> = black_box(
                black_box(buf)
                    .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(
                        IPV4_PACKET.metadata.src_ip,
                        IPV4_PACKET.metadata.dst_ip,
                    ))
                    .unwrap(),
            );
        })
    }

    bench!(bench_parse, bench_parse_inner);

    fn bench_serialize_inner<B: Bencher>(b: &mut B) {
        use crate::testdata::dns_request_v4::*;
        let builder = UdpPacketBuilder::new(
            IPV4_PACKET.metadata.src_ip,
            IPV4_PACKET.metadata.dst_ip,
            None,
            NonZeroU16::new(UDP_PACKET.metadata.dst_port).unwrap(),
        );
        let header_len = builder.constraints().header_len();
        let total_len = header_len + UDP_PACKET.bytes[UDP_PACKET.body_range].len();
        let mut buf = vec![0; total_len];
        buf[header_len..].copy_from_slice(&UDP_PACKET.bytes[UDP_PACKET.body_range]);

        b.iter(|| {
            let _: Buf<_> = black_box(
                black_box(Buf::new(&mut buf[..], header_len..total_len).encapsulate(builder))
                    .serialize_no_alloc_outer(),
            )
            .unwrap();
        })
    }

    bench!(bench_serialize, bench_serialize_inner);
}
