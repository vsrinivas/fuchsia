// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of UDP packets.

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::num::NonZeroU16;

use byteorder::{ByteOrder, NetworkEndian};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{Ip, IpAddress, IpProto};
use crate::wire::util::checksum::compute_transport_checksum;
use crate::wire::util::fits_in_u16;

pub(crate) const HEADER_BYTES: usize = 8;
const LENGTH_OFFSET: usize = 4;
const CHECKSUM_OFFSET: usize = 6;

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct Header {
    src_port: [u8; 2],
    dst_port: [u8; 2],
    length: [u8; 2],
    checksum: [u8; 2],
}

impl Header {
    fn src_port(&self) -> u16 {
        NetworkEndian::read_u16(&self.src_port)
    }

    fn set_src_port(&mut self, src_port: u16) {
        NetworkEndian::write_u16(&mut self.src_port, src_port);
    }

    fn dst_port(&self) -> u16 {
        NetworkEndian::read_u16(&self.dst_port)
    }

    fn set_dst_port(&mut self, dst_port: u16) {
        NetworkEndian::write_u16(&mut self.dst_port, dst_port);
    }

    fn length(&self) -> u16 {
        NetworkEndian::read_u16(&self.length)
    }

    fn checksum(&self) -> u16 {
        NetworkEndian::read_u16(&self.checksum)
    }
}

/// A UDP packet.
///
/// A `UdpPacket` shares its underlying memory with the byte slice it was parsed
/// from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// A `UdpPacket` - whether parsed using `parse` or created using `serialize` -
/// maintains the invariant that the checksum is always valid.
pub(crate) struct UdpPacket<B> {
    header: LayoutVerified<B, Header>,
    body: B,
}

/// Arguments required to parse a UDP packet.
pub(crate) struct UdpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> UdpParseArgs<A> {
    /// Construct a new `UdpParseArgs`.
    pub(crate) fn new(src_ip: A, dst_ip: A) -> UdpParseArgs<A> {
        UdpParseArgs { src_ip, dst_ip }
    }
}

impl<B: ByteSlice, A: IpAddress> ParsablePacket<B, UdpParseArgs<A>> for UdpPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: UdpParseArgs<A>) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let buf_len = buffer.as_ref().len();
        if buf_len < HEADER_BYTES {
            return debug_err!(Err(ParseError::Format), "too few bytes for header");
        }

        // We first read a few fields directly (rather than by doing
        // buffer.take_obj_front) since we need to compute the checksum over the
        // entire buffer, and so need to feed the buffer unmodified to
        // compute_transport_checksum.
        let length_field = NetworkEndian::read_u16(&buffer.as_ref()[LENGTH_OFFSET..]);
        let checksum = NetworkEndian::read_u16(&buffer.as_ref()[CHECKSUM_OFFSET..]);
        let real_length = if length_field == 0 && A::Version::VERSION.is_v6() {
            // IPv6 supports jumbograms, so a UDP packet may be greater than
            // 2^16 bytes in size. In this case, the size doesn't fit in the
            // 16-bit length field in the header, and so the length field is set
            // to zero to indicate this.
            buf_len
        } else {
            length_field.into()
        };
        if real_length > buf_len || real_length < HEADER_BYTES {
            return debug_err!(
                Err(ParseError::Format),
                "length field shorter than header or longer than buffer"
            );
        }

        // A 0 checksum indicates that the checksum wasn't computed. In IPv4,
        // this means that it shouldn't be validated. In IPv6, the checksum is
        // mandatory, so this is an error.
        if checksum != 0 {
            // When computing the checksum, a checksum of 0 is sent as 0xFFFF.
            // Since we normally expect the sum of all bytes including the
            // checksum to be 0, but we've added an extra factor of (0xFFFF - 0)
            // = 0xFFFF, we expect the sum to be 0xFFFF.
            let target = if checksum == 0xFFFF { 0xFFFF } else { 0 };
            let checksum = compute_transport_checksum(
                args.src_ip,
                args.dst_ip,
                IpProto::Udp,
                &buffer.as_ref()[..real_length],
            )
            .ok_or_else(debug_err_fn!(ParseError::Format, "packet too large"))?;
            if target != checksum {
                return debug_err!(Err(ParseError::Checksum), "invalid checksum");
            }
        } else if A::Version::VERSION.is_v6() {
            return debug_err!(Err(ParseError::Format), "missing checksum");
        }

        let header = buffer.take_obj_front::<Header>().expect("header length already validated");
        // Discard any padding left by the previous layer. The unwrap is safe
        // because we've already validated that real_length <= buf_len and that
        // real_length >= HEADER_BYTES. Thus, even though we've already consumed
        // HEADER_BYTES bytes from the buffer, we know that there are at least
        // buf_len - real_length bytes left.
        buffer.take_back(buf_len - real_length).unwrap();
        let packet = UdpPacket { header, body: buffer.into_rest() };
        if packet.header.dst_port() == 0 {
            return debug_err!(Err(ParseError::Format), "zero destination port");
        }

        Ok(packet)
    }
}

impl<B: ByteSlice> UdpPacket<B> {
    /// The packet body.
    pub(crate) fn body(&self) -> &[u8] {
        self.body.deref()
    }

    /// The source UDP port, if any.
    ///
    /// The source port is optional, and may have been omitted by the sender.
    pub(crate) fn src_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(self.header.src_port())
    }

    /// The destination UDP port.
    pub(crate) fn dst_port(&self) -> NonZeroU16 {
        // Infallible because it was validated in parse.
        NonZeroU16::new(self.header.dst_port()).unwrap()
    }

    /// Did this packet have a checksum?
    ///
    /// On IPv4, the sender may optionally omit the checksum. If this function
    /// returns false, the sender ommitted the checksum, and `parse` will not
    /// have validated it.
    ///
    /// On IPv6, it is guaranteed that `checksummed` will return true because
    /// IPv6 requires a checksum, and so any UDP packet missing one will fail
    /// validation in `parse`.
    pub(crate) fn checksummed(&self) -> bool {
        self.header.checksum() != 0
    }

    // The length of the header.
    fn header_len(&self) -> usize {
        self.header.bytes().len()
    }

    // The length of the packet as calculated from the header and body. This is
    // not the same as the length field in the header.
    fn total_packet_len(&self) -> usize {
        self.header_len() + self.body.len()
    }

    /// Construct a builder with the same contents as this packet.
    pub(crate) fn builder<A: IpAddress>(&self, src_ip: A, dst_ip: A) -> UdpPacketBuilder<A> {
        UdpPacketBuilder { src_ip, dst_ip, src_port: self.src_port(), dst_port: self.dst_port() }
    }
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the UDP packet; the only way to set them
// is via UdpPacketBuilder::serialize. This, combined with checksum validation
// performed in UdpPacket::parse, provides the invariant that a UdpPacket always
// has a valid checksum.

/// A builder for UDP packets.
#[derive(Copy, Clone, Debug)]
pub(crate) struct UdpPacketBuilder<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
    src_port: Option<NonZeroU16>,
    dst_port: NonZeroU16,
}

impl<A: IpAddress> UdpPacketBuilder<A> {
    /// Construct a new `UdpPacketBuilder`.
    pub(crate) fn new(
        src_ip: A,
        dst_ip: A,
        src_port: Option<NonZeroU16>,
        dst_port: NonZeroU16,
    ) -> UdpPacketBuilder<A> {
        UdpPacketBuilder { src_ip, dst_ip, src_port, dst_port }
    }
}

impl<A: IpAddress> PacketBuilder for UdpPacketBuilder<A> {
    fn header_len(&self) -> usize {
        HEADER_BYTES
    }

    fn min_body_len(&self) -> usize {
        0
    }

    fn max_body_len(&self) -> usize {
        if A::Version::VERSION.is_v4() {
            (1 << 16) - 1
        } else {
            // IPv6 supports jumbograms, so a UDP packet may be greater than
            // 2^16 bytes. In this case, the size doesn't fit in the 16-bit
            // length field in the header, and so the length field is set to
            // zero. That means that, from this packet's perspective, there's no
            // effective limit on the body size.
            std::usize::MAX
        }
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize(self, mut buffer: SerializeBuffer) {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to
        // prevent leaking information from packets previously stored in
        // this buffer.
        let header = header.take_obj_front_zero::<Header>().expect("too few bytes for UDP header");
        let mut packet = UdpPacket { header, body };

        packet.header.set_src_port(self.src_port.map(NonZeroU16::get).unwrap_or(0));
        packet.header.set_dst_port(self.dst_port.get());
        let total_len = packet.total_packet_len();
        let len_field = if fits_in_u16(total_len) {
            total_len as u16
        } else if A::Version::VERSION.is_v6() {
            // See comment in max_body_len
            0u16
        } else {
            panic!(
                "total UDP packet length of {} bytes overflows 16-bit length field of UDP header",
                total_len
            );
        };
        NetworkEndian::write_u16(&mut packet.header.length, len_field);
        // Initialize the checksum to 0 so that we will get the correct
        // value when we compute it below.
        NetworkEndian::write_u16(&mut packet.header.checksum, 0);

        // NOTE: We stop using packet at this point so that it no longer borrows
        // the buffer, and we can use the buffer directly.
        let packet_len = buffer.as_ref().len();
        let checksum =
            compute_transport_checksum(self.src_ip, self.dst_ip, IpProto::Udp, buffer.as_ref())
                .unwrap_or_else(|| {
                    panic!(
                    "total UDP packet length of {} bytes overflows length field of pseudo-header",
                    packet_len
                )
                });
        NetworkEndian::write_u16(
            &mut buffer.as_mut()[CHECKSUM_OFFSET..],
            if checksum == 0 {
                // When computing the checksum, a checksum of 0 is sent as 0xFFFF.
                0xFFFF
            } else {
                checksum
            },
        );
    }
}

// needed by Result::unwrap_err in the tests below
#[cfg(test)]
impl<B> Debug for UdpPacket<B> {
    fn fmt(&self, fmt: &mut Formatter) -> fmt::Result {
        write!(fmt, "UdpPacket")
    }
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, ParseBuffer, Serializer};

    use super::*;
    use crate::device::ethernet::EtherType;
    use crate::ip::{Ipv4Addr, Ipv6Addr};
    use crate::wire::ethernet::EthernetFrame;
    use crate::wire::ipv4::Ipv4Packet;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr =
        Ipv6Addr::new([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    #[test]
    fn test_parse_serialize_full() {
        use crate::wire::testdata::dns_request::*;

        let mut buf = &ETHERNET_FRAME_BYTES[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        assert_eq!(frame.src_mac(), ETHERNET_SRC_MAC);
        assert_eq!(frame.dst_mac(), ETHERNET_DST_MAC);
        assert_eq!(frame.ethertype(), Some(EtherType::Ipv4));

        let mut body = frame.body();
        let ip_packet = body.parse::<Ipv4Packet<_>>().unwrap();
        assert_eq!(ip_packet.proto(), IpProto::Udp);
        assert_eq!(ip_packet.dscp(), IP_DSCP);
        assert_eq!(ip_packet.ecn(), IP_ECN);
        assert_eq!(ip_packet.df_flag(), IP_DONT_FRAGMENT);
        assert_eq!(ip_packet.mf_flag(), IP_MORE_FRAGMENTS);
        assert_eq!(ip_packet.fragment_offset(), IP_FRAGMENT_OFFSET);
        assert_eq!(ip_packet.id(), IP_ID);
        assert_eq!(ip_packet.ttl(), IP_TTL);
        assert_eq!(ip_packet.src_ip(), IP_SRC_IP);
        assert_eq!(ip_packet.dst_ip(), IP_DST_IP);

        let mut body = ip_packet.body();
        let udp_packet = body
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(
                ip_packet.src_ip(),
                ip_packet.dst_ip(),
            ))
            .unwrap();
        assert_eq!(udp_packet.src_port().map(NonZeroU16::get).unwrap_or(0), UDP_SRC_PORT);
        assert_eq!(udp_packet.dst_port().get(), UDP_DST_PORT);
        assert_eq!(udp_packet.body(), UDP_BODY);

        let buffer = udp_packet
            .body()
            .encapsulate(udp_packet.builder(ip_packet.src_ip(), ip_packet.dst_ip()))
            .encapsulate(ip_packet.builder())
            .encapsulate(frame.builder())
            .serialize_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME_BYTES);
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

        // length of 0 is allowed in IPv6
        let mut buf = &[0, 0, 1, 2, 0, 0, 0xFD, 0xD3][..];
        let packet = buf
            .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(TEST_SRC_IPV6, TEST_DST_IPV6))
            .unwrap();
        assert!(packet.src_port().is_none());
        assert_eq!(packet.dst_port().get(), NetworkEndian::read_u16(&[1, 2]));
        assert!(packet.checksummed());
        assert!(packet.body().is_empty());
    }

    #[test]
    fn test_serialize() {
        let mut buf = (&[])
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_outer()
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
        BufferSerializer::new_vec(Buf::new(&mut buf_0[..], HEADER_BYTES..))
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_outer()
            .unwrap();
        let mut buf_1 = [0xFF; HEADER_BYTES];
        BufferSerializer::new_vec(Buf::new(&mut buf_1[..], HEADER_BYTES..))
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            ))
            .serialize_outer()
            .unwrap();
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    fn test_parse_fail() {
        // Test thact while a given byte pattern optionally succeeds, zeroing out
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
    #[should_panic]
    fn test_serialize_fail_header_too_short() {
        UdpPacketBuilder::new(TEST_SRC_IPV4, TEST_DST_IPV4, None, NonZeroU16::new(1).unwrap())
            .serialize(SerializeBuffer::new(&mut [0; 7][..], ..));
    }

    #[test]
    #[should_panic]
    fn test_serialize_fail_packet_too_long_ipv4() {
        (&[0; (1 << 16) - HEADER_BYTES][..])
            .encapsulate(UdpPacketBuilder::new(
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                None,
                NonZeroU16::new(1).unwrap(),
            ))
            .serialize_outer()
            .unwrap();
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
    //         .serialize_outer()
    //         .unwrap();
    // }
}

#[cfg(all(test, feature = "benchmark"))]
mod benchmarks {
    use std::num::NonZeroU16;

    use packet::{ParseBuffer, Serializer};
    use test::{black_box, Bencher};

    use super::*;
    use crate::ip::Ipv4;
    use crate::testutil::parse_ip_packet_in_ethernet_frame;

    #[bench]
    fn bench_parse(b: &mut Bencher) {
        use crate::wire::testdata::dns_request::*;
        let bytes = parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME_BYTES).unwrap().0;

        b.iter(|| {
            let mut buf = bytes;
            black_box(
                black_box(buf)
                    .parse_with::<_, UdpPacket<_>>(UdpParseArgs::new(IP_SRC_IP, IP_DST_IP))
                    .unwrap(),
            );
        })
    }

    #[bench]
    fn bench_serialize(b: &mut Bencher) {
        use crate::wire::testdata::dns_request::*;
        let builder = UdpPacketBuilder::new(
            IP_SRC_IP,
            IP_DST_IP,
            None,
            NonZeroU16::new(UDP_DST_PORT).unwrap(),
        );
        let mut buf = vec![0; builder.header_len() + UDP_BODY.len()];
        buf[builder.header_len()..].copy_from_slice(UDP_BODY);

        b.iter(|| {
            black_box(black_box((&mut buf[..]).encapsulate(builder.clone())).serialize_outer());
        })
    }
}
