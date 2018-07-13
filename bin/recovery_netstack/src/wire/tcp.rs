// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of TCP segments.

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::num::NonZeroU16;
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use error::ParseError;
use ip::{Ip, IpAddr, IpProto};
use transport::tcp::TcpOption;
use wire::util::{fits_in_u16, fits_in_u32, BufferAndRange, Checksum, Options};

use self::options::TcpOptionImpl;

/// The maximum length of a TCP header in bytes.
///
/// When calling `TcpSegmentBuilder::serialize`, provide at least
/// `MAX_HEADER_LEN` bytes for the header in order to guarantee that `serialize`
/// will not panic.
pub const MAX_HEADER_LEN: usize = 60;

// HeaderPrefix has the same memory layout (thanks to repr(C, packed)) as TCP
// header prefix. Thus, we can simply reinterpret the bytes of the TCP header
// prefix as a HeaderPrefix and then safely access its fields. Note the
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
#[derive(Default)]
#[repr(C, packed)]
struct HeaderPrefix {
    src_port: [u8; 2],
    dst_port: [u8; 2],
    seq_num: [u8; 4],
    ack: [u8; 4],
    data_offset_reserved_flags: [u8; 2],
    window_size: [u8; 2],
    checksum: [u8; 2],
    urg_ptr: [u8; 2],
}

unsafe impl FromBytes for HeaderPrefix {}
unsafe impl AsBytes for HeaderPrefix {}
unsafe impl Unaligned for HeaderPrefix {}

impl HeaderPrefix {
    pub fn src_port(&self) -> u16 {
        NetworkEndian::read_u16(&self.src_port)
    }

    pub fn dst_port(&self) -> u16 {
        NetworkEndian::read_u16(&self.dst_port)
    }

    fn data_offset(&self) -> u8 {
        (NetworkEndian::read_u16(&self.data_offset_reserved_flags) >> 12) as u8
    }
}

/// A TCP segment.
///
/// A `TcpSegment` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// A `TcpSegment` - whether parsed using `parse` or created using
/// `TcpSegmentBuilder` - maintains the invariant that the checksum is always
/// valid.
pub struct TcpSegment<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    options: Options<B, TcpOptionImpl>,
    body: B,
}

impl<B: ByteSlice> TcpSegment<B> {
    /// Parse a TCP segment.
    ///
    /// `parse` parses `bytes` as a TCP segment and validates the checksum. It
    /// returns the byte range corresponding to the body within `bytes`. This
    /// can be useful when extracting the encapsulated payload to send to
    /// another layer of the stack.
    pub fn parse<A: IpAddr>(
        bytes: B, src_ip: A, dst_ip: A,
    ) -> Result<(TcpSegment<B>, Range<usize>), ParseError> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let (hdr_prefix, rest) = LayoutVerified::<B, HeaderPrefix>::new_from_prefix(bytes)
            .ok_or_else(debug_err_fn!(
                ParseError::Format,
                "too few bytes for header"
            ))?;
        let hdr_bytes = (hdr_prefix.data_offset() * 4) as usize;
        if hdr_bytes > hdr_prefix.bytes().len() + rest.len() || hdr_bytes < hdr_prefix.bytes().len()
        {
            return debug_err!(
                Err(ParseError::Format),
                "invalid data offset: {}",
                hdr_prefix.data_offset()
            );
        }
        let (options, body) = rest.split_at(hdr_bytes - hdr_prefix.bytes().len());
        let options = Options::parse(options).map_err(|_| ParseError::Format)?;
        let segment = TcpSegment {
            hdr_prefix,
            options,
            body,
        };

        if segment.hdr_prefix.src_port() == 0 || segment.hdr_prefix.dst_port() == 0 {
            return debug_err!(Err(ParseError::Format), "zero source or destination port");
        }

        let checksum = NetworkEndian::read_u16(&segment.hdr_prefix.checksum);
        if segment
            .compute_checksum(src_ip, dst_ip)
            .ok_or_else(debug_err_fn!(ParseError::Format, "segment too large"))?
            != checksum
        {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }

        let hdr_len = segment.hdr_prefix.bytes().len() + segment.options.bytes().len();
        let total_len = hdr_len + segment.body.len();
        Ok((segment, hdr_len..total_len))
    }

    /// Iterate over the TCP header options.
    pub fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = TcpOption> {
        self.options.iter()
    }

    // Compute the TCP checksum, skipping the checksum field itself. Returns
    // None if the segment size is too large.
    fn compute_checksum<A: IpAddr>(&self, src_ip: A, dst_ip: A) -> Option<u16> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#Checksum_computation
        let mut checksum = Checksum::new();
        checksum.add_bytes(src_ip.bytes());
        checksum.add_bytes(dst_ip.bytes());
        let total_len = self.total_segment_len();
        if A::Version::VERSION.is_v4() {
            checksum.add_bytes(&[0, IpProto::Tcp as u8]);
            // For IPv4, the "TCP length" field in the pseudo-header is 16 bits.
            if !fits_in_u16(total_len) {
                return None;
            }
            let mut l = [0; 2];
            NetworkEndian::write_u16(&mut l, total_len as u16);
            checksum.add_bytes(&l);
        } else {
            // For IPv6, the "TCP length" field in the pseudo-header is 32 bits.
            if !fits_in_u32(total_len) {
                return None;
            }
            let mut l = [0; 4];
            NetworkEndian::write_u32(&mut l, total_len as u32);
            checksum.add_bytes(&l);
            checksum.add_bytes(&[0, 0, 0, IpProto::Tcp as u8]);
        }
        // the checksum is at bytes 16 and 17; skip it
        checksum.add_bytes(&self.hdr_prefix.bytes()[..16]);
        checksum.add_bytes(&self.hdr_prefix.bytes()[18..]);
        checksum.add_bytes(self.options.bytes());
        checksum.add_bytes(&self.body);
        Some(checksum.checksum())
    }

    /// The segment body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// The source port.
    pub fn src_port(&self) -> NonZeroU16 {
        NonZeroU16::new(self.hdr_prefix.src_port()).unwrap()
    }

    /// The destination port.
    pub fn dst_port(&self) -> NonZeroU16 {
        NonZeroU16::new(self.hdr_prefix.dst_port()).unwrap()
    }

    /// The sequence number.
    pub fn seq_num(&self) -> u32 {
        NetworkEndian::read_u32(&self.hdr_prefix.seq_num)
    }

    /// The acknowledgement number.
    ///
    /// If the ACK flag is not set, `ack_num` returns `None`.
    pub fn ack_num(&self) -> Option<u32> {
        if self.get_flag(ACK_MASK) {
            Some(NetworkEndian::read_u32(&self.hdr_prefix.ack))
        } else {
            None
        }
    }

    fn get_flag(&self, mask: u16) -> bool {
        NetworkEndian::read_u16(&self.hdr_prefix.data_offset_reserved_flags) & mask > 0
    }

    /// The RST flag.
    pub fn rst(&self) -> bool {
        self.get_flag(RST_MASK)
    }

    /// The SYN flag.
    pub fn syn(&self) -> bool {
        self.get_flag(SYN_MASK)
    }

    /// The FIN flag.
    pub fn fin(&self) -> bool {
        self.get_flag(FIN_MASK)
    }

    /// The sender's window size.
    pub fn window_size(&self) -> u16 {
        NetworkEndian::read_u16(&self.hdr_prefix.window_size)
    }

    // The length of the header prefix and options.
    fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len() + self.options.bytes().len()
    }

    // The length of the segment as calculated from the header prefix, options,
    // and body.
    fn total_segment_len(&self) -> usize {
        self.header_len() + self.body.len()
    }
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the TCP segment; the only way to set
// them is via TcpSegmentBuilder. This, combined with checksum validation
// performed in TcpSegment::parse, provides the invariant that a UdpPacket
// always has a valid checksum.

/// A builder for TCP segments.
pub struct TcpSegmentBuilder {
    src_port: u16,
    dst_port: u16,
    seq_num: u32,
    ack_num: u32,
    flags: u16,
    window_size: u16,
}

impl TcpSegmentBuilder {
    /// Create a new `TcpSegmentBuilder`.
    ///
    /// Create a new builder for TCP segments. If `ack_num` is `Some`, then the
    /// ACK flag will be set.
    pub fn new(
        src_port: NonZeroU16, dst_port: NonZeroU16, seq_num: u32, ack_num: Option<u32>,
        window_size: u16,
    ) -> TcpSegmentBuilder {
        let flags = if ack_num.is_some() { 1 << 4 } else { 0 };
        TcpSegmentBuilder {
            src_port: src_port.get(),
            dst_port: dst_port.get(),
            seq_num,
            ack_num: ack_num.unwrap_or(0),
            flags,
            window_size,
        }
    }

    fn set_flag(&mut self, mask: u16, set: bool) {
        if set {
            self.flags |= mask;
        } else {
            self.flags &= !mask;
        }
    }

    /// Set the RST flag.
    pub fn rst(&mut self, rst: bool) {
        self.set_flag(RST_MASK, rst);
    }

    /// Set the SYN flag.
    pub fn syn(&mut self, syn: bool) {
        self.set_flag(SYN_MASK, syn);
    }

    /// Set the FIN flag.
    pub fn fin(&mut self, fin: bool) {
        self.set_flag(FIN_MASK, fin);
    }

    /// Serialize a TCP segment in an existing buffer.
    ///
    /// `serialize` creates a `TcpSegment` which uses the provided `buffer` for
    /// its storage, initializing all header fields and calculating the checksum
    /// from the present configuration. It treats `buffer.range()` as the
    /// segment body. It uses the last bytes of `buffer` before the body to
    /// store the header, and returns a new `BufferAndRange` with a range equal
    /// to the bytes of the TCP segment (including the header). This range can
    /// be used to indicate the range for encapsulation in another packet.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// (&mut buffer[512..]).copy_from_slice(body);
    /// let builder = TcpSegmentBuilder::new(src_port, dst_port, seq_num, ack_num, window_size);
    /// let buffer = builder.serialize(BufferAndRange::new(&mut buffer[..], 512..), src_ip, dst_ip);
    /// send_ip_packet(dst_ip, buffer);
    /// ```
    ///
    /// # Panics
    ///
    /// `serialize` panics if there is insufficient room preceding the body to
    /// store the TCP header. The caller can guarantee that there will be enough
    /// room by providing at least `MAX_HEADER_LEN` pre-body bytes.
    pub fn serialize<B: AsMut<[u8]>, A: IpAddr>(
        self, mut buffer: BufferAndRange<B>, src_ip: A, dst_ip: A,
    ) -> BufferAndRange<B> {
        let extend_backwards = {
            let (header, body, _) = buffer.parts_mut();
            // create a 0-byte slice for the options since we don't support
            // serializing options yet (NET-955)
            let (options, body) = body.split_at_mut(0);
            // SECURITY: Use _zeroed constructor to ensure we zero memory to prevent
            // leaking information from packets previously stored in this buffer.
            let (_, hdr_prefix) =
                LayoutVerified::<_, HeaderPrefix>::new_unaligned_from_suffix_zeroed(header)
                    .expect("too few bytes for TCP header");
            let options =
                Options::parse(options).expect("parsing an empty options slice should not fail");
            let mut segment = TcpSegment {
                hdr_prefix,
                options,
                body,
            };

            NetworkEndian::write_u16(&mut segment.hdr_prefix.src_port, self.src_port);
            NetworkEndian::write_u16(&mut segment.hdr_prefix.dst_port, self.dst_port);
            NetworkEndian::write_u32(&mut segment.hdr_prefix.seq_num, self.seq_num);
            NetworkEndian::write_u32(&mut segment.hdr_prefix.ack, self.ack_num);
            // Data Offset is hard-coded to 5 until we support serializing options
            NetworkEndian::write_u16(
                &mut segment.hdr_prefix.data_offset_reserved_flags,
                (5u16 << 12) | self.flags,
            );
            NetworkEndian::write_u16(&mut segment.hdr_prefix.window_size, self.window_size);
            // we don't support setting the Urgent Pointer
            NetworkEndian::write_u16(&mut segment.hdr_prefix.urg_ptr, 0);

            let segment_len = segment.total_segment_len();
            // This ignores the checksum field in the header, so it's fine that we
            // haven't set it yet, and so it could be filled with arbitrary bytes.
            let checksum = segment.compute_checksum(src_ip, dst_ip).expect(&format!(
                "total TCP segment length of {} bytes overflows length field of pseudo-header",
                segment_len
            ));
            NetworkEndian::write_u16(&mut segment.hdr_prefix.checksum, checksum);

            segment.header_len()
        };

        buffer.extend_backwards(extend_backwards);
        buffer
    }
}

const ACK_MASK: u16 = 0b10000;
const RST_MASK: u16 = 0b00100;
const SYN_MASK: u16 = 0b00010;
const FIN_MASK: u16 = 0b00001;

mod options {
    use std::mem;

    use byteorder::{ByteOrder, NetworkEndian};

    use transport::tcp::{TcpOption, TcpSackBlock};
    use wire::util::OptionImpl;

    fn parse_sack_block(bytes: &[u8]) -> TcpSackBlock {
        TcpSackBlock {
            left_edge: NetworkEndian::read_u32(bytes),
            right_edge: NetworkEndian::read_u32(&bytes[4..]),
        }
    }

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;
    const OPTION_KIND_MSS: u8 = 2;
    const OPTION_KIND_WINDOW_SCALE: u8 = 3;
    const OPTION_KIND_SACK_PERMITTED: u8 = 4;
    const OPTION_KIND_SACK: u8 = 5;
    const OPTION_KIND_TIMESTAMP: u8 = 8;

    pub struct TcpOptionImpl;

    impl OptionImpl for TcpOptionImpl {
        type Output = TcpOption;
        type Error = ();

        fn parse(kind: u8, data: &[u8]) -> Result<Option<TcpOption>, ()> {
            match kind {
                self::OPTION_KIND_EOL | self::OPTION_KIND_NOP => {
                    unreachable!("wire::util::Options promises to handle EOL and NOP")
                }
                self::OPTION_KIND_MSS => if data.len() != 2 {
                    Err(())
                } else {
                    Ok(Some(TcpOption::Mss(NetworkEndian::read_u16(&data))))
                },
                self::OPTION_KIND_WINDOW_SCALE => if data.len() != 1 {
                    Err(())
                } else {
                    Ok(Some(TcpOption::WindowScale(data[0])))
                },
                self::OPTION_KIND_SACK_PERMITTED => if !data.is_empty() {
                    Err(())
                } else {
                    Ok(Some(TcpOption::SackPermitted))
                },
                self::OPTION_KIND_SACK => match data.len() {
                    8 | 16 | 24 | 32 => {
                        let num_blocks = data.len() / mem::size_of::<TcpSackBlock>();
                        let mut blocks = [TcpSackBlock::default(); 4];
                        for i in 0..num_blocks {
                            blocks[i] = parse_sack_block(&data[i * 8..]);
                        }
                        Ok(Some(TcpOption::Sack {
                            blocks,
                            num_blocks: num_blocks as u8,
                        }))
                    }
                    _ => Err(()),
                },
                self::OPTION_KIND_TIMESTAMP => if data.len() != 8 {
                    Err(())
                } else {
                    let ts_val = NetworkEndian::read_u32(&data);
                    let ts_echo_reply = NetworkEndian::read_u32(&data[4..]);
                    Ok(Some(TcpOption::Timestamp {
                        ts_val,
                        ts_echo_reply,
                    }))
                },
                _ => Ok(None),
            }
        }
    }
}

// needed by Result::unwrap_err in the tests below
#[cfg(test)]
impl<B> Debug for TcpSegment<B> {
    fn fmt(&self, fmt: &mut Formatter) -> fmt::Result {
        write!(fmt, "TcpSegment")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use device::ethernet::EtherType;
    use ip::{IpProto, Ipv4Addr, Ipv6Addr};
    use wire::ethernet::EthernetFrame;
    use wire::ipv4::Ipv4Packet;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr = Ipv6Addr::new([
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
    ]);

    #[test]
    fn test_parse_full() {
        use wire::testdata::tls_client_hello::*;

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

        let (segment, body_range) =
            TcpSegment::parse(packet.body(), packet.src_ip(), packet.dst_ip()).unwrap();
        assert_eq!(body_range, TCP_BODY_RANGE);
        assert_eq!(segment.src_port().get(), TCP_SRC_PORT);
        assert_eq!(segment.dst_port().get(), TCP_DST_PORT);
        assert_eq!(segment.ack_num().is_some(), TCP_ACK_FLAG);
        assert_eq!(segment.fin(), TCP_FIN_FLAG);
        assert_eq!(segment.syn(), TCP_SYN_FLAG);
        assert_eq!(
            segment.iter_options().collect::<Vec<_>>().as_slice(),
            TCP_OPTIONS
        );
        assert_eq!(segment.body(), TCP_BODY);
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
    // checksum (assuming no body and the src/dst IPs TEST_SRC_IPV4 and
    // TEST_DST_IPV4).
    fn new_hdr_prefix() -> HeaderPrefix {
        let mut hdr_prefix = HeaderPrefix::default();
        hdr_prefix.src_port = [0, 1];
        hdr_prefix.dst_port = [0, 2];
        // data offset of 5
        NetworkEndian::write_u16(&mut hdr_prefix.data_offset_reserved_flags, 5u16 << 12);
        hdr_prefix.checksum = [0x9f, 0xce];
        hdr_prefix
    }

    #[test]
    fn test_parse() {
        let bytes = hdr_prefix_to_bytes(new_hdr_prefix());
        let (segment, body_range) =
            TcpSegment::parse(&bytes[..], TEST_SRC_IPV4, TEST_DST_IPV4).unwrap();
        assert_eq!(body_range, 20..20);
        assert_eq!(segment.src_port().get(), 1);
        assert_eq!(segment.dst_port().get(), 2);
        assert_eq!(segment.body(), []);
    }

    #[test]
    fn test_parse_error() {
        // Set the source port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.src_port = [0, 0];
        assert_eq!(
            TcpSegment::parse(
                &hdr_prefix_to_bytes(hdr_prefix)[..],
                TEST_SRC_IPV4,
                TEST_DST_IPV4
            ).unwrap_err(),
            ParseError::Format
        );

        // Set the destination port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.dst_port = [0, 0];
        assert_eq!(
            TcpSegment::parse(
                &hdr_prefix_to_bytes(hdr_prefix)[..],
                TEST_SRC_IPV4,
                TEST_DST_IPV4
            ).unwrap_err(),
            ParseError::Format
        );

        // Set the data offset to 4, implying a header length of 16. This is
        // smaller than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        NetworkEndian::write_u16(&mut hdr_prefix.data_offset_reserved_flags, 4u16 << 12);
        assert_eq!(
            TcpSegment::parse(
                &hdr_prefix_to_bytes(hdr_prefix)[..],
                TEST_SRC_IPV4,
                TEST_DST_IPV4
            ).unwrap_err(),
            ParseError::Format
        );

        // Set the data offset to 6, implying a header length of 24. This is
        // larger than the actual segment length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        NetworkEndian::write_u16(&mut hdr_prefix.data_offset_reserved_flags, 6u16 << 12);
        assert_eq!(
            TcpSegment::parse(
                &hdr_prefix_to_bytes(hdr_prefix)[..],
                TEST_SRC_IPV4,
                TEST_DST_IPV4
            ).unwrap_err(),
            ParseError::Format
        );
    }

    // Return a stock TcpSegmentBuilder with reasonable default values.
    fn new_builder() -> TcpSegmentBuilder {
        TcpSegmentBuilder::new(
            NonZeroU16::new(1).unwrap(),
            NonZeroU16::new(2).unwrap(),
            3,
            Some(4),
            5,
        )
    }

    #[test]
    fn test_serialize() {
        let mut buf = [0; 30];
        let mut builder = new_builder();
        builder.fin(true);
        builder.rst(true);
        builder.syn(true);
        {
            // set the body
            (&mut buf[20..]).copy_from_slice(&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9]);
            let buffer = builder.serialize(
                BufferAndRange::new(&mut buf[..], 20..),
                TEST_SRC_IPV4,
                TEST_DST_IPV4,
            );
            assert_eq!(buffer.range(), 0..30);
        }

        // assert that we get the literal bytes we expected
        assert_eq!(
            buf,
            [
                0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 80, 23, 0, 5, 141, 137, 0, 0, 0, 1, 2, 3, 3, 4,
                5, 7, 8, 9
            ]
        );
        let (segment, body_range) =
            TcpSegment::parse(&buf[..], TEST_SRC_IPV4, TEST_DST_IPV4).unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(body_range, 20..30);
        assert_eq!(segment.src_port().get(), 1);
        assert_eq!(segment.dst_port().get(), 2);
        assert_eq!(segment.seq_num(), 3);
        assert_eq!(segment.ack_num(), Some(4));
        assert_eq!(segment.window_size(), 5);
        assert_eq!(segment.body(), [0, 1, 2, 3, 3, 4, 5, 7, 8, 9]);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that TcpSegmentBuilder::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; 20];
        new_builder().serialize(
            BufferAndRange::new(&mut buf_0[..], 20..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
        );
        let mut buf_1 = [0xFF; 20];
        new_builder().serialize(
            BufferAndRange::new(&mut buf_1[..], 20..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
        );
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_body_range() {
        // Test that a body range which is out of bounds of the buffer is
        // rejected.
        new_builder().serialize(
            BufferAndRange::new(&mut [0; 20][..], ..21),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
        );
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_insufficient_header_space() {
        // Test that a body range which doesn't leave enough room for the header
        // is rejected.
        new_builder().serialize(
            BufferAndRange::new(&mut [0; 20][..], ..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
        );
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_segment_too_long_ipv4() {
        // Test that a segment length which overflows u16 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        new_builder().serialize(
            BufferAndRange::new(&mut [0; 1 << 16][..], 20..),
            TEST_SRC_IPV4,
            TEST_DST_IPV4,
        );
    }

    #[test]
    #[ignore] // this test panics with stack overflow; TODO(joshlf): Fix
    #[should_panic]
    #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    fn test_serialize_panic_segment_too_long_ipv6() {
        // Test that a segment length which overflows u32 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        new_builder().serialize(
            BufferAndRange::new(&mut [0; 1 << 32][..], 20..),
            TEST_SRC_IPV6,
            TEST_DST_IPV6,
        );
    }
}
