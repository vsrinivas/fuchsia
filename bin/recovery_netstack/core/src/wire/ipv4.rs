// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv4 packets.

use std::fmt::{self, Debug, Formatter};
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::ParseError;
use crate::ip::{IpProto, Ipv4Addr, Ipv4Option};
use crate::wire::util::{BufferAndRange, Checksum, Options};

use self::options::Ipv4OptionImpl;

const HEADER_PREFIX_SIZE: usize = 20;

// HeaderPrefix has the same memory layout (thanks to repr(C, packed)) as an
// IPv4 header prefix. Thus, we can simply reinterpret the bytes of the IPv4
// header prefix as a HeaderPrefix and then safely access its fields. Note the
// following caveats:
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
pub struct HeaderPrefix {
    version_ihl: u8,
    dscp_ecn: u8,
    total_len: [u8; 2],
    id: [u8; 2],
    flags_frag_off: [u8; 2],
    ttl: u8,
    proto: u8,
    hdr_checksum: [u8; 2],
    src_ip: [u8; 4],
    dst_ip: [u8; 4],
}

/// The maximum length of an IPv4 header in bytes.
///
/// When calling `Ipv4PacketBuilder::build`, provide at least `MAX_HEADER_LEN`
/// bytes for the header in order to guarantee that `serialize` will not panic.
pub const MAX_HEADER_LEN: usize = 60;

/// The minimum length of an IPv4 header in bytes.
///
/// When calculating the number of IPv4 body bytes required to meet a certain
/// minimum body size requirement of an encapsulating packet format, the IPv4
/// header is guaranteed to consume at least `MIN_HEADER_LEN` bytes.
pub const MIN_HEADER_LEN: usize = 20;

unsafe impl FromBytes for HeaderPrefix {}
unsafe impl AsBytes for HeaderPrefix {}
unsafe impl Unaligned for HeaderPrefix {}

impl HeaderPrefix {
    fn version(&self) -> u8 {
        self.version_ihl >> 4
    }

    /// Get the Internet Header Length (IHL).
    pub fn ihl(&self) -> u8 {
        self.version_ihl & 0xF
    }

    fn total_length(&self) -> u16 {
        NetworkEndian::read_u16(&self.total_len)
    }

    fn hdr_checksum(&self) -> u16 {
        NetworkEndian::read_u16(&self.hdr_checksum)
    }
}

/// An IPv4 packet.
///
/// An `Ipv4Packet` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// An `Ipv4Packet` - whether parsed using `parse` or created using
/// `Ipv4PacketBuilder` - maintains the invariant that the checksum is always
/// valid.
pub struct Ipv4Packet<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    options: Options<B, Ipv4OptionImpl>,
    body: B,
}

impl<B: ByteSlice> Ipv4Packet<B> {
    /// Parse an IPv4 packet.
    ///
    /// `parse` parses `bytes` as an IPv4 packet and validates the checksum. It
    /// returns the byte range corresponding to the body within `bytes`. This
    /// can be useful when extracting the encapsulated payload to send to
    /// another layer of the stack.
    pub fn parse(bytes: B) -> Result<(Ipv4Packet<B>, Range<usize>), ParseError> {
        // See for details: https://en.wikipedia.org/wiki/IPv4#Header

        let total_len = bytes.len();
        let (hdr_prefix, rest) = LayoutVerified::<B, HeaderPrefix>::new_from_prefix(bytes)
            .ok_or_else(debug_err_fn!(
                ParseError::Format,
                "too few bytes for header"
            ))?;
        let hdr_bytes = (hdr_prefix.ihl() * 4) as usize;
        if hdr_bytes > total_len || hdr_bytes < hdr_prefix.bytes().len() {
            return debug_err!(Err(ParseError::Format), "invalid IHL: {}", hdr_prefix.ihl());
        }
        let (options, body) = rest.split_at(hdr_bytes - HEADER_PREFIX_SIZE);
        let options = Options::parse(options).map_err(|_| ParseError::Format)?;
        if hdr_prefix.version() != 4 {
            return debug_err!(
                Err(ParseError::Format),
                "unexpected IP version: {}",
                hdr_prefix.version()
            );
        }
        let body = if (hdr_prefix.total_length() as usize) < total_len {
            let (body, _) = body.split_at(hdr_prefix.total_length() as usize - hdr_bytes);
            body
        } else if hdr_prefix.total_length() as usize == total_len {
            body
        } else {
            // we don't yet support IPv4 fragmentation
            return debug_err!(Err(ParseError::NotSupported), "fragmentation not supported");
        };

        let packet = Ipv4Packet {
            hdr_prefix,
            options,
            body,
        };
        if packet.compute_header_checksum() != packet.hdr_prefix.hdr_checksum() {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }
        let hdr_len = packet.hdr_prefix.bytes().len() + packet.options.bytes().len();
        Ok((packet, hdr_len..total_len))
    }

    /// Iterate over the IPv4 header options.
    pub fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = Ipv4Option> {
        self.options.iter()
    }

    // Compute the header checksum, skipping the checksum field itself.
    fn compute_header_checksum(&self) -> u16 {
        let mut c = Checksum::new();
        // the header checksum is at bytes 10 and 11
        c.add_bytes(&self.hdr_prefix.bytes()[..10]);
        c.add_bytes(&self.hdr_prefix.bytes()[12..]);
        c.add_bytes(self.options.bytes());
        c.checksum()
    }

    /// The packet body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// The Differentiated Services Code Point (DSCP).
    pub fn dscp(&self) -> u8 {
        self.hdr_prefix.dscp_ecn >> 2
    }

    /// The Explicit Congestion Notification (ECN).
    pub fn ecn(&self) -> u8 {
        self.hdr_prefix.dscp_ecn & 3
    }

    /// The identification.
    pub fn id(&self) -> u16 {
        NetworkEndian::read_u16(&self.hdr_prefix.id)
    }

    /// The Don't Fragment (DF) flag.
    pub fn df_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.hdr_prefix.flags_frag_off[0] & (1 << (5 + DF_FLAG_OFFSET)) > 0
    }

    /// The More Fragments (MF) flag.
    pub fn mf_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.hdr_prefix.flags_frag_off[0] & (1 << (5 + MF_FLAG_OFFSET)) > 0
    }

    /// The fragment offset.
    pub fn fragment_offset(&self) -> u16 {
        ((u16::from(self.hdr_prefix.flags_frag_off[0] & 0x1F)) << 8)
            | u16::from(self.hdr_prefix.flags_frag_off[1])
    }

    /// The Time To Live (TTL).
    pub fn ttl(&self) -> u8 {
        self.hdr_prefix.ttl
    }

    /// The IP Protocol.
    ///
    /// `proto` returns the `IpProto` from the protocol field. If the protocol
    /// number is unrecognized, the `Err` value returned contains the numerical
    /// protocol number.
    pub fn proto(&self) -> Result<IpProto, u8> {
        IpProto::from_u8(self.hdr_prefix.proto).ok_or(self.hdr_prefix.proto)
    }

    /// The source IP address.
    pub fn src_ip(&self) -> Ipv4Addr {
        Ipv4Addr::new(self.hdr_prefix.src_ip)
    }

    /// The destination IP address.
    pub fn dst_ip(&self) -> Ipv4Addr {
        Ipv4Addr::new(self.hdr_prefix.dst_ip)
    }

    // The size of the header prefix and options.
    fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len() + self.options.bytes().len()
    }

    // The size of the packet as calculated from the header prefix, options, and
    // body. This is not the same as the total length field in the header.
    fn total_packet_len(&self) -> usize {
        self.header_len() + self.body.len()
    }
}

impl<B> Ipv4Packet<B>
where
    B: ByteSliceMut,
{
    /// Set the Time To Live (TTL).
    ///
    /// Set the TTL and update the header checksum accordingly.
    pub fn set_ttl(&mut self, ttl: u8) {
        // See the Checksum::update documentation for why we need to provide two
        // bytes which are at an even byte offset from the beginning of the
        // header.
        let old_bytes = [self.hdr_prefix.ttl, self.hdr_prefix.proto];
        let new_bytes = [ttl, self.hdr_prefix.proto];
        let checksum = Checksum::update(self.hdr_prefix.hdr_checksum(), &old_bytes, &new_bytes);
        NetworkEndian::write_u16(&mut self.hdr_prefix.hdr_checksum, checksum);
        self.hdr_prefix.ttl = ttl;
    }
}

impl<B> Debug for Ipv4Packet<B>
where
    B: ByteSlice,
{
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("Ipv4Packet")
            .field("src_ip", &self.src_ip())
            .field("dst_ip", &self.dst_ip())
            .field("id", &self.id())
            .field("ttl", &self.ttl())
            .field("proto", &self.proto())
            .field("frag_off", &self.fragment_offset())
            .field("dscp", &self.dscp())
            .field("ecn", &self.ecn())
            .field("mf_flag", &self.mf_flag())
            .field("df_flag", &self.df_flag())
            .field("body", &format!("<{} bytes>", self.body.len()))
            .finish()
    }
}

/// A builder for IPv4 packets.
pub struct Ipv4PacketBuilder {
    dscp: u8,
    ecn: u8,
    id: u16,
    flags: u8,
    frag_off: u16,
    ttl: u8,
    proto: u8,
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
}

impl Ipv4PacketBuilder {
    /// Create a new `Ipv4PacketBuilder`.
    ///
    /// Create a new builder for IPv4 packets.
    pub fn new(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, ttl: u8, proto: IpProto) -> Ipv4PacketBuilder {
        Ipv4PacketBuilder {
            dscp: 0,
            ecn: 0,
            id: 0,
            flags: 0,
            frag_off: 0,
            ttl,
            proto: proto as u8,
            src_ip,
            dst_ip,
        }
    }

    /// Set the Differentiated Services Code Point (DSCP).
    ///
    /// # Panics
    ///
    /// `dscp` panics if `dscp` is greater than 2^6 - 1.
    pub fn dscp(&mut self, dscp: u8) {
        assert!(dscp <= 1 << 6, "invalid DCSP: {}", dscp);
        self.dscp = dscp;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    pub fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 3, "invalid ECN: {}", ecn);
        self.ecn = ecn;
    }

    /// Set the identification.
    pub fn id(&mut self, id: u16) {
        self.id = id;
    }

    /// Set the Don't Fragment (DF) flag.
    pub fn df_flag(&mut self, df: bool) {
        if df {
            self.flags |= 1 << DF_FLAG_OFFSET;
        } else {
            self.flags &= !(1 << DF_FLAG_OFFSET);
        }
    }

    /// Set the More Fragments (MF) flag.
    pub fn mf_flag(&mut self, mf: bool) {
        if mf {
            self.flags |= 1 << MF_FLAG_OFFSET;
        } else {
            self.flags &= !(1 << MF_FLAG_OFFSET);
        }
    }

    /// Set the fragment offset.
    ///
    /// # Panics
    ///
    /// `fragment_offset` panics if `fragment_offset` is greater than 2^13 - 1.
    pub fn fragment_offset(&mut self, fragment_offset: u16) {
        assert!(
            fragment_offset < 1 << 13,
            "invalid fragment offset: {}",
            fragment_offset
        );
        self.frag_off = fragment_offset;
    }

    /// Serialize an IPv4 packet in an existing buffer.
    ///
    /// `serialize` creates an `Ipv4Packet` which uses the provided `buffer` for its
    /// storage, initializing all header fields from the present configuration.
    /// It treats `buffer.range()` as the packet body. It uses the last bytes of
    /// `buffer` before the body to store the header, and returns a new
    /// `BufferAndRange` with a range equal to the bytes of the IPv4 packet
    /// (including the header). This range can be used to indicate the range for
    /// encapsulation in another packet.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// (&mut buffer[512..]).copy_from_slice(body);
    /// let builder = Ipv4PacketBuilder::new(src_ip, dst_ip, ttl, proto);
    /// let buffer = builder.serialize(BufferAndRange::new(&mut buffer[..], 512..));
    /// send_ip_frame(device, buffer);
    /// ```
    ///
    /// # Panics
    ///
    /// `serialize` panics if there is insufficient room preceding the body to store
    /// the IPv4 header. The caller can guarantee that there will be enough room
    /// by providing at least `MAX_HEADER_LEN` pre-body bytes.
    pub fn serialize<B: AsMut<[u8]>>(self, mut buffer: BufferAndRange<B>) -> BufferAndRange<B> {
        let extend_backwards = {
            let (header, body, _) = buffer.parts_mut();
            // create a 0-byte slice for the options since we don't support
            // serializing options yet (NET-955)
            let (options, body) = body.split_at_mut(0);
            // SECURITY: Use _zeroed constructor to ensure we zero memory to prevent
            // leaking information from packets previously stored in this buffer.
            let (_, hdr_prefix) =
                LayoutVerified::<_, HeaderPrefix>::new_unaligned_from_suffix_zeroed(header)
                    .expect("too few bytes for IPv4 header");
            let options =
                Options::parse(options).expect("parsing an empty options slice should not fail");
            let mut packet = Ipv4Packet {
                hdr_prefix,
                options,
                body,
            };

            packet.hdr_prefix.version_ihl = (4u8 << 4) | 5;
            packet.hdr_prefix.dscp_ecn = (self.dscp << 2) | self.ecn;
            let total_len = packet.total_packet_len();
            if total_len >= 1 << 16 {
                panic!(
                    "packet length of {} exceeds maximum of {}",
                    total_len,
                    1 << 16 - 1,
                );
            }
            NetworkEndian::write_u16(&mut packet.hdr_prefix.total_len, total_len as u16);
            NetworkEndian::write_u16(&mut packet.hdr_prefix.id, self.id);
            NetworkEndian::write_u16(
                &mut packet.hdr_prefix.flags_frag_off,
                ((u16::from(self.flags)) << 13) | self.frag_off,
            );
            packet.hdr_prefix.ttl = self.ttl;
            packet.hdr_prefix.proto = self.proto;
            packet.hdr_prefix.src_ip = self.src_ip.ipv4_bytes();
            packet.hdr_prefix.dst_ip = self.dst_ip.ipv4_bytes();
            let checksum = packet.compute_header_checksum();
            NetworkEndian::write_u16(&mut packet.hdr_prefix.hdr_checksum, checksum);

            packet.header_len()
        };

        buffer.extend_backwards(extend_backwards);
        buffer
    }
}

// bit positions into the flags bits
const DF_FLAG_OFFSET: u32 = 1;
const MF_FLAG_OFFSET: u32 = 0;

mod options {
    use crate::ip::{Ipv4Option, Ipv4OptionData};
    use crate::wire::util::OptionImpl;

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;

    pub struct Ipv4OptionImpl;

    impl OptionImpl for Ipv4OptionImpl {
        type Output = Ipv4Option;
        type Error = ();

        fn parse(kind: u8, data: &[u8]) -> Result<Option<Ipv4Option>, ()> {
            let copied = kind & (1 << 7) > 0;
            match kind {
                self::OPTION_KIND_EOL | self::OPTION_KIND_NOP => {
                    unreachable!("wire::util::Options promises to handle EOL and NOP")
                }
                kind => if data.len() > 38 {
                    Err(())
                } else {
                    let len = data.len();
                    let mut d = [0u8; 38];
                    (&mut d[..len]).copy_from_slice(data);
                    Ok(Some(Ipv4Option {
                        copied,
                        data: Ipv4OptionData::Unrecognized {
                            kind,
                            len: len as u8,
                            data: d,
                        },
                    }))
                },
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::device::ethernet::EtherType;
    use crate::wire::ethernet::EthernetFrame;

    const DEFAULT_SRC_IP: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const DEFAULT_DST_IP: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);

    #[test]
    fn test_parse_full_tcp() {
        use crate::wire::testdata::tls_client_hello::*;

        let (frame, body_range) = EthernetFrame::parse(ETHERNET_FRAME_BYTES).unwrap();
        assert_eq!(body_range, ETHERNET_BODY_RANGE);
        assert_eq!(frame.src_mac(), ETHERNET_SRC_MAC);
        assert_eq!(frame.dst_mac(), ETHERNET_DST_MAC);
        assert_eq!(frame.ethertype(), Some(Ok(EtherType::Ipv4)));

        let (packet, body_range) = Ipv4Packet::parse(frame.body()).unwrap();
        assert_eq!(body_range, IP_BODY_RANGE);
        assert_eq!(packet.proto(), Ok(IpProto::Tcp));
        assert_eq!(packet.dscp(), IP_DSCP);
        assert_eq!(packet.ecn(), IP_ECN);
        assert_eq!(packet.df_flag(), IP_DONT_FRAGMENT);
        assert_eq!(packet.mf_flag(), IP_MORE_FRAGMENTS);
        assert_eq!(packet.fragment_offset(), IP_FRAGMENT_OFFSET);
        assert_eq!(packet.id(), IP_ID);
        assert_eq!(packet.ttl(), IP_TTL);
        assert_eq!(packet.src_ip(), IP_SRC_IP);
        assert_eq!(packet.dst_ip(), IP_DST_IP);
    }

    #[test]
    fn test_parse_full_udp() {
        use crate::wire::testdata::dns_request::*;

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
    }

    fn hdr_prefix_to_bytes(hdr_prefix: HeaderPrefix) -> [u8; 20] {
        let mut bytes = [0; 20];
        {
            let mut lv = LayoutVerified::new_unaligned(&mut bytes[..]).unwrap();
            *lv = hdr_prefix;
        }
        bytes
    }

    // Return a new HeaderPrefix with reasonable defaults, including a valid
    // header checksum.
    fn new_hdr_prefix() -> HeaderPrefix {
        let mut hdr_prefix = HeaderPrefix::default();
        hdr_prefix.version_ihl = (4 << 4) | 5;
        NetworkEndian::write_u16(&mut hdr_prefix.total_len[..], 20);
        NetworkEndian::write_u16(&mut hdr_prefix.id[..], 0x0102);
        hdr_prefix.ttl = 0x03;
        hdr_prefix.proto = IpProto::Tcp as u8;
        hdr_prefix.src_ip = DEFAULT_SRC_IP.ipv4_bytes();
        hdr_prefix.dst_ip = DEFAULT_DST_IP.ipv4_bytes();
        hdr_prefix.hdr_checksum = [0xa6, 0xcf];
        hdr_prefix
    }

    #[test]
    fn test_parse() {
        let bytes = hdr_prefix_to_bytes(new_hdr_prefix());
        let (packet, body_range) = Ipv4Packet::parse(&bytes[..]).unwrap();
        assert_eq!(body_range, 20..20);
        assert_eq!(packet.id(), 0x0102);
        assert_eq!(packet.ttl(), 0x03);
        assert_eq!(packet.proto(), Ok(IpProto::Tcp));
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), []);
    }

    #[test]
    fn test_parse_error() {
        // Set the version to 5. The version must be 4.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (5 << 4) | 5;
        assert_eq!(
            Ipv4Packet::parse(&hdr_prefix_to_bytes(hdr_prefix)[..]).unwrap_err(),
            ParseError::Format
        );

        // Set the IHL to 4, implying a header length of 16. This is smaller
        // than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 4;
        assert_eq!(
            Ipv4Packet::parse(&hdr_prefix_to_bytes(hdr_prefix)[..]).unwrap_err(),
            ParseError::Format
        );

        // Set the IHL to 6, implying a header length of 24. This is larger than
        // the actual packet length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 6;
        assert_eq!(
            Ipv4Packet::parse(&hdr_prefix_to_bytes(hdr_prefix)[..]).unwrap_err(),
            ParseError::Format
        );
    }

    // Return a stock Ipv4PacketBuilder with reasonable default values.
    fn new_builder() -> Ipv4PacketBuilder {
        Ipv4PacketBuilder::new(DEFAULT_DST_IP, DEFAULT_DST_IP, 64, IpProto::Tcp)
    }

    #[test]
    fn test_serialize() {
        let mut buf = [0; 30];
        let mut builder = new_builder();
        builder.dscp(0x12);
        builder.ecn(3);
        builder.id(0x0405);
        builder.df_flag(true);
        builder.mf_flag(true);
        builder.fragment_offset(0x0607);
        {
            // set the body
            (&mut buf[20..]).copy_from_slice(&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9]);
            let buffer = builder.serialize(BufferAndRange::new(&mut buf[..], 20..));
            assert_eq!(buffer.range(), 0..30);
        }

        // assert that we get the literal bytes we expected
        assert_eq!(
            buf,
            [
                69, 75, 0, 30, 4, 5, 102, 7, 64, 6, 248, 103, 5, 6, 7, 8, 5, 6, 7, 8, 0, 1, 2, 3,
                3, 4, 5, 7, 8, 9
            ],
        );
        let (packet, body_range) = Ipv4Packet::parse(&buf[..]).unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(body_range, 20..30);
        assert_eq!(packet.dscp(), 0x12);
        assert_eq!(packet.ecn(), 3);
        assert_eq!(packet.id(), 0x0405);
        assert!(packet.df_flag());
        assert!(packet.mf_flag());
        assert_eq!(packet.fragment_offset(), 0x0607);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that Ipv4PacketBuilder::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; 20];
        new_builder().serialize(BufferAndRange::new(&mut buf_0[..], 20..));
        let mut buf_1 = [0xFF; 20];
        new_builder().serialize(BufferAndRange::new(&mut buf_1[..], 20..));
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_packet_length() {
        // Test that a packet which is longer than 2^16 - 1 bytes is rejected.
        new_builder().serialize(BufferAndRange::new(&mut [0; 1 << 16][..], 20..));
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_insufficient_header_space() {
        // Test that a body range which doesn't leave enough room for the header
        // is rejected.
        new_builder().serialize(BufferAndRange::new(&mut [0; 20], ..));
    }
}
