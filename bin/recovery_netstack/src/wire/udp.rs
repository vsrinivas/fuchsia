// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of UDP packets.

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::num::NonZeroU16;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use error::ParseError;
use ip::{Ip, IpAddr, IpProto};
use wire::util::{fits_in_u16, fits_in_u32, BufferAndRange, Checksum};

// Header has the same memory layout (thanks to repr(C, packed)) as a UDP
// header. Thus, we can simply reinterpret the bytes of the UDP header as a
// Header and then safely access its fields. Note the following caveats:
// - We cannot make any guarantees about the alignment of an instance of this
//   struct in memory or of any of its fields. This is true both because
//   repr(packed) removes the padding that would be used to ensure the alignment
//   of individual fields, but also because we are given no guarantees about
//   where within a given memory buffer a particular packet (and thus its
//   header) will be located.
// - Individual fields are all either u8 or [u8; N] rather than u16, u32, etc.
//   This is for two reasons:
//   - u16 and larger have larger-than-1 alignments, which are forbidden as
//     described above
//   - We are not guaranteed that the local platform has the same endianness as
//     network byte order (big endian), so simply treating a sequence of bytes
//     as a u16 or other multi-byte number would not necessarily be correct.
//     Instead, we use the NetworkEndian type and its reader and writer methods
//     to correctly access these fields.
#[repr(C, packed)]
struct Header {
    src_port: [u8; 2],
    dst_port: [u8; 2],
    length: [u8; 2],
    checksum: [u8; 2],
}

/// The length of a UDP header in bytes.
///
/// When calling `UdpPacket::serialize`, provide at least `HEADER_LEN` bytes for
/// the header in order to guarantee that `serialize` will not panic.
pub const HEADER_LEN: usize = 8;

unsafe impl FromBytes for Header {}
unsafe impl AsBytes for Header {}
unsafe impl Unaligned for Header {}

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
pub struct UdpPacket<B> {
    header: LayoutVerified<B, Header>,
    body: B,
}

impl<B: ByteSlice> UdpPacket<B> {
    /// Parse a UDP packet.
    ///
    /// `parse` parses `bytes` as a UDP packet and validates the checksum. It
    /// returns the byte range corresponding to the body within `bytes`. This
    /// can be useful when extracting the encapsulated payload to send to
    /// another layer of the stack.
    ///
    /// `src_ip` is the source address in the IP header. In IPv4, `dst_ip` is
    /// the destination address in the IPv4 header. In IPv6, it's more
    /// complicated:
    /// - If there's no routing header, the destination is the one in the IPv6
    ///   header.
    /// - If there is a routing header, then the sender will compute the
    ///   checksum using the last address in the routing header, while the
    ///   receiver will compute the checksum using the destination address in
    ///   the IPv6 header.
    pub fn parse<A: IpAddr>(
        bytes: B, src_ip: A, dst_ip: A,
    ) -> Result<(UdpPacket<B>, Range<usize>), ParseError> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let bytes_len = bytes.len();
        let (header, body) =
            LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
                debug_err_fn!(ParseError::Format, "too few bytes for header"),
            )?;
        let packet = UdpPacket { header, body };
        let len = if packet.header.length() == 0 && A::Version::VERSION.is_v6() {
            // IPv6 supports jumbograms, so a UDP packet may be greater than
            // 2^16 bytes in size. In this case, the size doesn't fit in the
            // 16-bit length field in the header, and so the length field is set
            // to zero to indicate this.
            bytes_len
        } else {
            packet.header.length() as usize
        };
        if len != bytes_len {
            return debug_err!(
                Err(ParseError::Format),
                "length in header does not match packet length"
            );
        }
        if packet.header.dst_port() == 0 {
            return debug_err!(Err(ParseError::Format), "zero destination port");
        }

        // A 0 checksum indicates that the checksum wasn't computed. In IPv4,
        // this means that it shouldn't be validated. In IPv6, the checksum is
        // mandatory, so this is an error.
        if packet.header.checksum != [0, 0] {
            // When computing the checksum, a checksum of 0 is sent as 0xFFFF.
            let target = if packet.header.checksum == [0xFF, 0xFF] {
                0
            } else {
                NetworkEndian::read_u16(&packet.header.checksum)
            };
            if packet
                .compute_checksum(src_ip, dst_ip)
                .ok_or_else(debug_err_fn!(ParseError::Format, "segment too large"))?
                != target
            {
                return debug_err!(Err(ParseError::Checksum), "invalid checksum");
            }
        } else if A::Version::VERSION.is_v6() {
            return debug_err!(Err(ParseError::Format), "missing checksum");
        }

        let hdr_len = packet.header.bytes().len();
        let total_len = hdr_len + packet.body.len();
        Ok((packet, hdr_len..total_len))
    }
}

impl<B: ByteSlice> UdpPacket<B> {
    // Compute the UDP checksum, skipping the checksum field itself. Returns
    // None if the packet size is too large.
    fn compute_checksum<A: IpAddr>(&self, src_ip: A, dst_ip: A) -> Option<u16> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Checksum_computation
        let mut c = Checksum::new();
        c.add_bytes(src_ip.bytes());
        c.add_bytes(dst_ip.bytes());
        if A::Version::VERSION.is_v4() {
            c.add_bytes(&[0, IpProto::Udp as u8]);
            c.add_bytes(&self.header.length);
        } else {
            let len = self.total_packet_len();
            // For IPv6, the "UDP length" field in the pseudo-header is 32 bits.
            if !fits_in_u32(len) {
                return None;
            }
            let mut len_bytes = [0; 4];
            NetworkEndian::write_u32(&mut len_bytes, len as u32);
            c.add_bytes(&len_bytes);
            c.add_bytes(&[0, 0, 0, IpProto::Udp as u8]);
        }
        c.add_bytes(&self.header.src_port);
        c.add_bytes(&self.header.dst_port);
        c.add_bytes(&self.header.length);
        c.add_bytes(&self.body);
        Some(c.checksum())
    }

    /// The packet body.
    pub fn body(&self) -> &[u8] {
        self.body.deref()
    }

    /// The source UDP port, if any.
    ///
    /// The source port is optional, and may have been omitted by the sender.
    pub fn src_port(&self) -> Option<NonZeroU16> {
        NonZeroU16::new(self.header.src_port())
    }

    /// The destination UDP port.
    pub fn dst_port(&self) -> NonZeroU16 {
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
    pub fn checksummed(&self) -> bool {
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
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the UDP packet; the only way to set them
// is via UdpPacket::serialize. This, combined with checksum validation performed
// in UdpPacket::parse, provides the invariant that a UdpPacket always has a
// valid checksum.

impl<B> UdpPacket<B>
where
    B: AsMut<[u8]>,
{
    /// Serialize a UDP packet in an existing buffer.
    ///
    /// `serialize` creates a `UdpPacket` which uses the provided `buffer` for
    /// its storage, initializing all header fields and calculating the
    /// checksum. It treats `buffer.range()` as the packet body. It uses the
    /// last bytes of `buffer` before the body to store the header, and returns
    /// a new `BufferAndRange` with a range equal to the bytes of the UDP packet
    /// (including the header). This range can be used to indicate the range for
    /// encapsulation in another packet.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// (&mut buffer[512..]).copy_from_slice(body);
    /// let buffer = UdpPacket::serialize(
    ///     BufferAndRange::new(&mut buffer[..], 512..),
    ///     src_ip,
    ///     dst_ip,
    ///     src_port,
    ///     dst_port,
    /// );
    /// send_ip_packet(dst_ip, buffer);
    /// ```
    ///
    /// # Panics
    ///
    /// `serialize` panics if there is insufficient room preceding the body to
    /// store the UDP header. The caller can guarantee that there will be enough
    /// room by providing at least `MAX_HEADER_LEN` pre-body bytes.
    pub fn serialize<A: IpAddr>(
        mut buffer: BufferAndRange<B>, src_ip: A, dst_ip: A, src_port: Option<NonZeroU16>,
        dst_port: NonZeroU16,
    ) -> BufferAndRange<B> {
        // See for details: https://en.wikipedia.org/wiki/User_Datagram_Protocol#Packet_structure

        let extend_backwards = {
            let (header, body, _) = buffer.parts_mut();
            // SECURITY: Use _zeroed constructor to ensure we zero memory to prevent
            // leaking information from packets previously stored in this buffer.
            let (_, header) = LayoutVerified::<_, Header>::new_unaligned_from_suffix_zeroed(header)
                .expect("too few bytes for UDP header");
            let mut packet = UdpPacket { header, body };

            packet
                .header
                .set_src_port(src_port.map(|port| port.get()).unwrap_or(0));
            packet.header.set_dst_port(dst_port.get());
            let total_len = packet.total_packet_len();
            let len_field = if fits_in_u16(total_len) {
                total_len as u16
            } else if A::Version::VERSION.is_v6() {
                // IPv6 supports jumbograms, so a UDP packet may be greater than
                // 2^16 bytes in size. In this case, the size doesn't fit in the
                // 16-bit length field in the header, and so the length field is set
                // to zero to indicate this.
                0u16
            } else {
                panic!(
                    "total UDP packet length of {} bytes overflows 16-bit length field of UDP \
                     header",
                    total_len
                );
            };
            NetworkEndian::write_u16(&mut packet.header.length, len_field);

            // This ignores the checksum field in the header, so it's fine that we
            // haven't set it yet, and so it could be filled with arbitrary bytes.
            let c = packet.compute_checksum(src_ip, dst_ip).expect(&format!(
                "total UDP packet length of {} bytes overflow 32-bit length field of pseudo-header",
                total_len
            ));
            NetworkEndian::write_u16(
                &mut packet.header.checksum,
                if c == 0 {
                    // When computing the checksum, a checksum of 0 is sent as 0xFFFF.
                    0xFFFF
                } else {
                    c
                },
            );

            packet.header_len()
        };

        buffer.extend_backwards(extend_backwards);
        buffer
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
    use device::ethernet::EtherType;
    use ip::{Ipv4Addr, Ipv6Addr};
    use wire::ethernet::EthernetFrame;
    use wire::ipv4::Ipv4Packet;

    use super::*;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr = Ipv6Addr::new([
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    ]);

    #[test]
    fn test_parse_full() {
        use wire::testdata::dns_request::*;

        let (frame, body_range) = EthernetFrame::parse(ETHERNET_FRAME_BYTES).unwrap();
        assert_eq!(body_range, ETHERNET_BODY_RANGE);
        assert_eq!(frame.src_mac(), ETHERNET_SRC_MAC);
        assert_eq!(frame.dst_mac(), ETHERNET_DST_MAC);
        assert_eq!(frame.ethertype(), Some(Ok(EtherType::Ipv4)));

        let (packet, body_range) = Ipv4Packet::parse(frame.body()).unwrap();
        assert_eq!(body_range, IP_BODY_RANGE);
        assert_eq!(packet.proto(), Ok(IpProto::Udp));
        assert_eq!(packet.dscp(), IP_DSCP);
        assert_eq!(packet.ecn(), IP_ECN);
        assert_eq!(packet.df_flag(), IP_DONT_FRAGMENT);
        assert_eq!(packet.mf_flag(), IP_MORE_FRAGMENTS);
        assert_eq!(packet.fragment_offset(), IP_FRAGMENT_OFFSET);
        assert_eq!(packet.id(), IP_ID);
        assert_eq!(packet.ttl(), IP_TTL);
        assert_eq!(packet.src_ip(), IP_SRC_IP);
        assert_eq!(packet.dst_ip(), IP_DST_IP);

        let (packet, body_range) =
            UdpPacket::parse(packet.body(), packet.src_ip(), packet.dst_ip()).unwrap();
        assert_eq!(body_range, UDP_BODY_RANGE);
        assert_eq!(
            packet.src_port().map(|p| p.get()).unwrap_or(0),
            UDP_SRC_PORT
        );
        assert_eq!(packet.dst_port().get(), UDP_DST_PORT);
        assert_eq!(packet.body(), UDP_BODY);
    }

    #[test]
    fn test_parse() {
        // source port of 0 (meaning none) is allowed, as is a missing checksum
        let buf = [0, 0, 1, 2, 0, 8, 0, 0];
        let (packet, body_range) =
            UdpPacket::parse(&buf[..], TEST_SRC_IPV4, TEST_DST_IPV4).unwrap();
        assert_eq!(body_range, 8..8);
        assert!(packet.src_port().is_none());
        assert_eq!(packet.dst_port().get(), NetworkEndian::read_u16(&[1, 2]));
        assert!(!packet.checksummed());
        assert!(packet.body().is_empty());

        // length of 0 is allowed in IPv6
        let buf = [0, 0, 1, 2, 0, 0, 0xFD, 0xD3];
        let (packet, body_range) =
            UdpPacket::parse(&buf[..], TEST_SRC_IPV6, TEST_DST_IPV6).unwrap();
        assert_eq!(body_range, 8..8);
        assert!(packet.src_port().is_none());
        assert_eq!(packet.dst_port().get(), NetworkEndian::read_u16(&[1, 2]));
        assert!(packet.checksummed());
        assert!(packet.body().is_empty());
    }

    #[test]
    fn test_serialize() {
        let mut buf = [0; 8];
        {
            let buffer = UdpPacket::serialize(
                BufferAndRange::new(&mut buf, 8..),
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
                NonZeroU16::new(1),
                NonZeroU16::new(2).unwrap(),
            );
            assert_eq!(buffer.range(), 0..8);
        }
        // assert that we get the literal bytes we expected
        assert_eq!(buf, [0, 1, 0, 2, 0, 8, 239, 199]);
        let (packet, _) = UdpPacket::parse(&buf[..], TEST_SRC_IPV4, TEST_DST_IPV4).unwrap();
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
        let mut buf_0 = [0; 8];
        UdpPacket::serialize(
            BufferAndRange::new(&mut buf_0[..], 8..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            NonZeroU16::new(1),
            NonZeroU16::new(2).unwrap(),
        );
        let mut buf_1 = [0xFF; 8];
        UdpPacket::serialize(
            BufferAndRange::new(&mut buf_1[..], 8..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            NonZeroU16::new(1),
            NonZeroU16::new(2).unwrap(),
        );
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    fn test_parse_fail() {
        // Test that while a given byte pattern optionally succeeds, zeroing out
        // certain bytes causes failure. `zero` is a list of byte indices to
        // zero out that should cause failure.
        fn test_zero<I: IpAddr>(src: I, dst: I, succeeds: bool, zero: &[usize], err: ParseError) {
            // Set checksum to zero so that, in IPV4, it will be ignored. In
            // IPv6, this /is/ the test.
            let mut buf = [1, 2, 3, 4, 0, 8, 0, 0];
            if succeeds {
                assert!(UdpPacket::parse(&buf[..], src, dst).is_ok());
            }
            for idx in zero {
                buf[*idx] = 0;
            }
            assert_eq!(UdpPacket::parse(&buf[..], src, dst).unwrap_err(), err);
        }

        // destination port of 0 is disallowed
        test_zero(
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            true,
            &[2, 3],
            ParseError::Format,
        );
        // length of 0 is disallowed in IPv4
        test_zero(
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            true,
            &[4, 5],
            ParseError::Format,
        );
        // missing checksum is disallowed in IPv6; this won't succeed ahead of
        // time because the checksum bytes are already zero
        test_zero(TEST_SRC_IPV6, TEST_DST_IPV6, false, &[], ParseError::Format);

        // 2^32 overflows on 32-bit platforms
        #[cfg(target_pointer_width = "64")]
        {
            // total length of 2^32 or greater is disallowed in IPv6
            let mut buf = vec![0u8; 1 << 32];
            (&mut buf[..8]).copy_from_slice(&[0, 0, 1, 2, 0, 0, 0xFF, 0xE4]);
            assert_eq!(
                UdpPacket::parse(&buf[..], TEST_SRC_IPV6, TEST_DST_IPV6).unwrap_err(),
                ParseError::Format
            );
        }
    }

    #[test]
    #[should_panic]
    fn test_serialize_fail_header_too_short() {
        let mut buf = [0; 7];
        UdpPacket::serialize(
            BufferAndRange::new(&mut buf[..], 7..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            None,
            NonZeroU16::new(1).unwrap(),
        );
    }

    #[test]
    #[should_panic]
    fn test_serialize_fail_packet_too_long_ipv4() {
        let mut buf = [0; 1 << 16];
        UdpPacket::serialize(
            BufferAndRange::new(&mut buf[..], 8..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            None,
            NonZeroU16::new(1).unwrap(),
        );
    }

    #[test]
    #[should_panic]
    #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    fn test_serialize_fail_packet_too_long_ipv6() {
        // total length of 2^32 or greater is disallowed in IPv6
        let mut buf = vec![0u8; 1 << 32];
        UdpPacket::serialize(
            BufferAndRange::new(&mut buf[..], 8..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
            None,
            NonZeroU16::new(1).unwrap(),
        );
    }
}
