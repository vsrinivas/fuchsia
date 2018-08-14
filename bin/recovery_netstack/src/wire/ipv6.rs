// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 packets.

use std::fmt::{self, Debug, Formatter};
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use log::{debug, log};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{IpProto, Ipv6Addr};
use crate::wire::util::BufferAndRange;

const FIXED_HEADER_SIZE: usize = 40;

// FixedHeader has the same memory layout (thanks to repr(C, packed)) as an IPv6
// fixed header. Thus, we can simply reinterpret the bytes of the IPv6 fixed
// header as a FixedHeader and then safely access its fields. Note the following
// caveats:
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
#[allow(missing_docs)]
#[derive(Default)]
#[repr(C, packed)]
pub struct FixedHeader {
    version_tc_flowlabel: [u8; 4],
    payload_len: [u8; 2],
    next_hdr: u8,
    hop_limit: u8,
    src_ip: [u8; 16],
    dst_ip: [u8; 16],
}

/// The minimum length of an IPv6 header in bytes.
///
/// When calculating the number of IPv6 body bytes required to meet a certain
/// minimum body size requirement of an encapsulating packet format, the IPv6
/// header is guaranteed to consume at least `MIN_HEADER_LEN` bytes.
pub const MIN_HEADER_LEN: usize = 40;

unsafe impl FromBytes for FixedHeader {}
unsafe impl AsBytes for FixedHeader {}
unsafe impl Unaligned for FixedHeader {}

impl FixedHeader {
    fn version(&self) -> u8 {
        self.version_tc_flowlabel[0] >> 4
    }

    // TODO(tkilbourn): split this into DS and ECN
    fn traffic_class(&self) -> u8 {
        (self.version_tc_flowlabel[0] & 0xF) << 4 | self.version_tc_flowlabel[1] >> 4
    }

    fn flowlabel(&self) -> u32 {
        (self.version_tc_flowlabel[1] as u32 & 0xF) << 16
            | (self.version_tc_flowlabel[2] as u32) << 8
            | self.version_tc_flowlabel[3] as u32
    }

    fn payload_len(&self) -> u16 {
        NetworkEndian::read_u16(&self.payload_len)
    }
}

/// An IPv6 packet.
///
/// An `Ipv6Packet` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub struct Ipv6Packet<B> {
    fixed_hdr: LayoutVerified<B, FixedHeader>,
    extension_hdrs: Option<B>,
    body: B,
}

impl<B: ByteSlice> Ipv6Packet<B> {
    /// Parse an IPv6 packet.
    ///
    /// `parse` parses `bytes` as an IPv6 packet. It returns the byte range
    /// corresponding to the body within `bytes`. This can be useful when
    /// extracting the encapsulated payload to send to another layer of the
    /// stack.
    pub fn parse(bytes: B) -> Result<(Ipv6Packet<B>, Range<usize>), ParseError> {
        let total_len = bytes.len();
        let (fixed_hdr, rest) = LayoutVerified::<B, FixedHeader>::new_from_prefix(bytes)
            .ok_or_else(debug_err_fn!(
                ParseError::Format,
                "too few bytes for header"
            ))?;
        // TODO(tkilbourn): support extension headers
        let packet = Ipv6Packet {
            fixed_hdr,
            extension_hdrs: None,
            body: rest,
        };
        if packet.fixed_hdr.version() != 6 {
            return debug_err!(
                Err(ParseError::Format),
                "unexpected IP version: {}",
                packet.fixed_hdr.version()
            );
        }
        if packet.body.len() != packet.fixed_hdr.payload_len() as usize {
            return debug_err!(
                Err(ParseError::Format),
                "payload length does not match header"
            );
        }
        let extensions_len = packet.extension_hdrs.as_ref().map(|b| b.len()).unwrap_or(0);
        let hdr_len = packet.fixed_hdr.bytes().len() + extensions_len;
        Ok((packet, hdr_len..total_len))
    }

    /// The packet body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// The Differentiated Services (DS) field.
    pub fn ds(&self) -> u8 {
        self.fixed_hdr.traffic_class() >> 2
    }

    /// The Explicit Congestion Notification (ECN).
    pub fn ecn(&self) -> u8 {
        self.fixed_hdr.traffic_class() & 0b11
    }

    /// The flow label.
    pub fn flowlabel(&self) -> u32 {
        self.fixed_hdr.flowlabel()
    }

    /// The hop limit.
    pub fn hop_limit(&self) -> u8 {
        self.fixed_hdr.hop_limit
    }

    /// The IP Protocol.
    pub fn proto(&self) -> Result<IpProto, u8> {
        // TODO(tkilbourn): support extension headers
        IpProto::from_u8(self.fixed_hdr.next_hdr).ok_or(self.fixed_hdr.next_hdr)
    }

    /// The source IP address.
    pub fn src_ip(&self) -> Ipv6Addr {
        Ipv6Addr::new(self.fixed_hdr.src_ip)
    }

    /// The destination IP address.
    pub fn dst_ip(&self) -> Ipv6Addr {
        Ipv6Addr::new(self.fixed_hdr.dst_ip)
    }

    fn header_len(&self) -> usize {
        let extensions_len = self.extension_hdrs.as_ref().map(|b| b.len()).unwrap_or(0);
        self.fixed_hdr.bytes().len() + extensions_len
    }

    fn payload_len(&self) -> usize {
        self.extension_hdrs.as_ref().map(|b| b.len()).unwrap_or(0) + self.body.len()
    }
}

impl<B: ByteSliceMut> Ipv6Packet<B> {
    /// Set the hop limit.
    pub fn set_hop_limit(&mut self, hlim: u8) {
        self.fixed_hdr.hop_limit = hlim;
    }
}

impl<B: ByteSlice> Debug for Ipv6Packet<B> {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("Ipv6Packet")
            .field("src_ip", &self.src_ip())
            .field("dst_ip", &self.dst_ip())
            .field("hop_limit", &self.hop_limit())
            .field("proto", &self.proto())
            .field("ds", &self.ds())
            .field("ecn", &self.ecn())
            .field("flowlabel", &self.flowlabel())
            .field("extension headers", &"TODO")
            .field("body", &format!("<{} bytes>", self.body.len()))
            .finish()
    }
}

/// A builder for IPv6 packets.
pub struct Ipv6PacketBuilder {
    ds: u8,
    ecn: u8,
    flowlabel: u32,
    hop_limit: u8,
    proto: u8,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
}

impl Ipv6PacketBuilder {
    /// Create a new `Ipv6PacketBuilder`.
    pub fn new(
        src_ip: Ipv6Addr, dst_ip: Ipv6Addr, hop_limit: u8, proto: IpProto,
    ) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: 0,
            ecn: 0,
            flowlabel: 0,
            hop_limit,
            proto: proto as u8,
            src_ip,
            dst_ip,
        }
    }

    /// Set the Differentiated Services (DS).
    ///
    /// # Panics
    ///
    /// `ds` panics if `ds` is greater than 2^6 - 1.
    pub fn ds(&mut self, ds: u8) {
        assert!(ds <= 1 << 6, "invalid DS: {}", ds);
        self.ds = ds;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    pub fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 0b11, "invalid ECN: {}", ecn);
        self.ecn = ecn
    }

    /// Set the flowlabel.
    ///
    /// # Panics
    ///
    /// `flowlabel` panics if `flowlabel` is greater than 2^20 - 1.
    pub fn flowlabel(&mut self, flowlabel: u32) {
        assert!(flowlabel <= 1 << 20, "invalid flowlabel: {:x}", flowlabel);
        self.flowlabel = flowlabel;
    }

    /// Serialize an IPv6 packet in an existing buffer.
    ///
    /// `serialize` creates an `Ipv6Packet` which uses the provided `buffer` for
    /// its storage, initializing all header fields from the present
    /// configuration. It treats `buffer.range()` as the packet body. It uses
    /// the last bytes of `buffer` before the body to store the header, and
    /// returns a new `BufferAndRange` with a range equal to the bytes of the
    /// IPv6 packet (including the header). This range can be used to indicate
    /// the range for encapsulation in another packet.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// (&mut buffer[512..]).copy_from_slice(body);
    /// let builder = Ipv6PacketBuilder::new(src_ip, dst_ip, hop_limit, proto);
    /// let buffer = builder.serialize(BufferAndRange::new(&mut buffer[..], 512..));
    /// send_ip_frame(device, buffer);
    /// ```
    pub fn serialize<B: AsMut<[u8]>>(self, mut buffer: BufferAndRange<B>) -> BufferAndRange<B> {
        let extend_backwards = {
            let (header, body, _) = buffer.parts_mut();
            // TODO(tkilbourn): support extension headers
            let (_, fixed_hdr) =
                LayoutVerified::<_, FixedHeader>::new_unaligned_from_suffix_zeroed(header)
                    .expect("too few bytes for IPv6 header");
            let extension_hdrs = None;
            let mut packet = Ipv6Packet {
                fixed_hdr,
                extension_hdrs,
                body,
            };

            packet.fixed_hdr.version_tc_flowlabel = [
                (6u8 << 4) | self.ds >> 2,
                ((self.ds & 0b11) << 6) | (self.ecn << 4) | (self.flowlabel >> 16) as u8,
                ((self.flowlabel >> 8) & 0xFF) as u8,
                (self.flowlabel & 0xFF) as u8,
            ];
            let payload_len = packet.payload_len();
            debug!("serialize: payload_len={}", payload_len);
            if payload_len >= 1 << 16 {
                panic!(
                    "packet length of {} exceeds maximum of {}",
                    payload_len,
                    1 << 16 - 1,
                );
            }
            NetworkEndian::write_u16(&mut packet.fixed_hdr.payload_len, payload_len as u16);
            packet.fixed_hdr.next_hdr = self.proto;
            packet.fixed_hdr.hop_limit = self.hop_limit;
            packet.fixed_hdr.src_ip = self.src_ip.ipv6_bytes();
            packet.fixed_hdr.dst_ip = self.dst_ip.ipv6_bytes();

            packet.header_len()
        };

        buffer.extend_backwards(extend_backwards);
        buffer
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DEFAULT_SRC_IP: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const DEFAULT_DST_IP: Ipv6Addr = Ipv6Addr::new([
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    ]);

    // TODO(tkilbourn): add IPv6 versions of TCP and UDP parsing

    fn fixed_hdr_to_bytes(fixed_hdr: FixedHeader) -> [u8; 40] {
        let mut bytes = [0; 40];
        {
            let mut lv = LayoutVerified::new_unaligned(&mut bytes[..]).unwrap();
            *lv = fixed_hdr;
        }
        bytes
    }

    // Return a new FixedHeader with reasonable defaults.
    fn new_fixed_hdr() -> FixedHeader {
        let mut fixed_hdr = FixedHeader::default();
        NetworkEndian::write_u32(&mut fixed_hdr.version_tc_flowlabel[..], 0x60200077);
        NetworkEndian::write_u16(&mut fixed_hdr.payload_len[..], 0);
        fixed_hdr.next_hdr = IpProto::Tcp as u8;
        fixed_hdr.hop_limit = 64;
        fixed_hdr.src_ip = DEFAULT_SRC_IP.ipv6_bytes();
        fixed_hdr.dst_ip = DEFAULT_DST_IP.ipv6_bytes();
        fixed_hdr
    }

    #[test]
    fn test_parse() {
        let bytes = fixed_hdr_to_bytes(new_fixed_hdr());
        let (packet, body_range) = Ipv6Packet::parse(&bytes[..]).unwrap();
        assert_eq!(body_range, 40..40);
        assert_eq!(packet.ds(), 0);
        assert_eq!(packet.ecn(), 2);
        assert_eq!(packet.flowlabel(), 0x77);
        assert_eq!(packet.hop_limit(), 64);
        assert_eq!(packet.proto(), Ok(IpProto::Tcp));
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), []);
    }

    #[test]
    fn test_parse_error() {
        // Set the version to 5. The version must be 6.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.version_tc_flowlabel[0] = 0x50;
        assert_eq!(
            Ipv6Packet::parse(&fixed_hdr_to_bytes(fixed_hdr)[..]).unwrap_err(),
            ParseError::Format
        );

        // Set the payload len to 2, even though there's no payload.
        let mut fixed_hdr = new_fixed_hdr();
        NetworkEndian::write_u16(&mut fixed_hdr.payload_len[..], 2);
        assert_eq!(
            Ipv6Packet::parse(&fixed_hdr_to_bytes(fixed_hdr)[..]).unwrap_err(),
            ParseError::Format
        );
    }

    // Return a stock Ipv6PacketBuilder with reasonable default values.
    fn new_builder() -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(DEFAULT_SRC_IP, DEFAULT_DST_IP, 64, IpProto::Tcp)
    }

    #[test]
    fn test_serialize() {
        let mut buf = [0; 50];
        let mut builder = new_builder();
        builder.ds(0x12);
        builder.ecn(3);
        builder.flowlabel(0x10405);
        {
            // set the body
            (&mut buf[40..]).copy_from_slice(&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
            let buffer = builder.serialize(BufferAndRange::new(&mut buf[..], 40..));
            assert_eq!(buffer.range(), 0..50);
        }

        // assert that we get the literal bytes we expected
        assert_eq!(
            &buf[..],
            &[
                100, 177, 4, 5, 0, 10, 6, 64, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 0, 1, 2, 3, 4,
                5, 6, 7, 8, 9
            ][..],
        );

        let (packet, body_range) = Ipv6Packet::parse(&buf[..]).unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(body_range, 40..50);
        assert_eq!(packet.ds(), 0x12);
        assert_eq!(packet.ecn(), 3);
        assert_eq!(packet.flowlabel(), 0x10405);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that Ipv6PacketBuilder::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; 40];
        new_builder().serialize(BufferAndRange::new(&mut buf_0[..], 40..));
        let mut buf_1 = [0xFF; 40];
        new_builder().serialize(BufferAndRange::new(&mut buf_1[..], 40..));
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_packet_length() {
        // Test that a packet which is longer than 2^16 - 1 bytes is rejected.
        new_builder().serialize(BufferAndRange::new(&mut [0; (1 << 16) + 40][..], 40..));
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_insufficient_header_space() {
        // Test that a body range which doesn't leave enough room for the header
        // is rejected.
        new_builder().serialize(BufferAndRange::new(&mut [0u8; 40][..], ..));
    }
}
