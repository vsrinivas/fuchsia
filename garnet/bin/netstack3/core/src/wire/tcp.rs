// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of TCP segments.

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::num::NonZeroU16;

use byteorder::{ByteOrder, NetworkEndian};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{IpAddress, IpProto};
use crate::transport::tcp::TcpOption;
use crate::wire::util::checksum::compute_transport_checksum;
use crate::wire::util::records::options::Options;

use self::options::TcpOptionsImpl;

const HDR_PREFIX_LEN: usize = 20;
const CHECKSUM_OFFSET: usize = 16;
pub(crate) const TCP_MIN_HDR_LEN: usize = HDR_PREFIX_LEN;
#[cfg(test)]
pub(crate) const TCP_MAX_HDR_LEN: usize = 60;

#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
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

impl HeaderPrefix {
    pub(crate) fn src_port(&self) -> u16 {
        NetworkEndian::read_u16(&self.src_port)
    }

    pub(crate) fn dst_port(&self) -> u16 {
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
pub(crate) struct TcpSegment<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    options: Options<B, TcpOptionsImpl>,
    body: B,
}

/// Arguments required to parse a TCP segment.
pub(crate) struct TcpParseArgs<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
}

impl<A: IpAddress> TcpParseArgs<A> {
    /// Construct a new `TcpParseArgs`.
    pub(crate) fn new(src_ip: A, dst_ip: A) -> TcpParseArgs<A> {
        TcpParseArgs { src_ip, dst_ip }
    }
}

impl<B: ByteSlice, A: IpAddress> ParsablePacket<B, TcpParseArgs<A>> for TcpSegment<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len() + self.options.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: TcpParseArgs<A>) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let checksum =
            compute_transport_checksum(args.src_ip, args.dst_ip, IpProto::Tcp, buffer.as_ref())
                .ok_or_else(debug_err_fn!(ParseError::Format, "segment too large"))?;
        if checksum != 0 {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }

        let hdr_prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let hdr_bytes = (hdr_prefix.data_offset() * 4) as usize;
        if hdr_bytes < hdr_prefix.bytes().len() {
            return debug_err!(
                Err(ParseError::Format),
                "invalid data offset: {}",
                hdr_prefix.data_offset()
            );
        }
        let options = buffer
            .take_front(hdr_bytes - hdr_prefix.bytes().len())
            .ok_or_else(debug_err_fn!(ParseError::Format, "data offset larger than buffer"))?;
        let options = Options::parse(options).map_err(|_| ParseError::Format)?;
        let segment = TcpSegment { hdr_prefix, options, body: buffer.into_rest() };

        if segment.hdr_prefix.src_port() == 0 || segment.hdr_prefix.dst_port() == 0 {
            return debug_err!(Err(ParseError::Format), "zero source or destination port");
        }

        Ok(segment)
    }
}

impl<B: ByteSlice> TcpSegment<B> {
    /// Iterate over the TCP header options.
    pub(crate) fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = TcpOption> {
        self.options.iter()
    }

    /// The segment body.
    pub(crate) fn body(&self) -> &[u8] {
        &self.body
    }

    /// The source port.
    pub(crate) fn src_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.src_port()).unwrap()
    }

    /// The destination port.
    pub(crate) fn dst_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.dst_port()).unwrap()
    }

    /// The sequence number.
    pub(crate) fn seq_num(&self) -> u32 {
        NetworkEndian::read_u32(&self.hdr_prefix.seq_num)
    }

    /// The acknowledgement number.
    ///
    /// If the ACK flag is not set, `ack_num` returns `None`.
    pub(crate) fn ack_num(&self) -> Option<u32> {
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
    pub(crate) fn rst(&self) -> bool {
        self.get_flag(RST_MASK)
    }

    /// The SYN flag.
    pub(crate) fn syn(&self) -> bool {
        self.get_flag(SYN_MASK)
    }

    /// The FIN flag.
    pub(crate) fn fin(&self) -> bool {
        self.get_flag(FIN_MASK)
    }

    /// The sender's window size.
    pub(crate) fn window_size(&self) -> u16 {
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

    /// Construct a builder with the same contents as this packet.
    pub(crate) fn builder<A: IpAddress>(&self, src_ip: A, dst_ip: A) -> TcpSegmentBuilder<A> {
        let mut s = TcpSegmentBuilder {
            src_ip,
            dst_ip,
            src_port: self.src_port().get(),
            dst_port: self.dst_port().get(),
            seq_num: self.seq_num(),
            ack_num: NetworkEndian::read_u32(&self.hdr_prefix.ack),
            flags: 0,
            window_size: self.window_size(),
        };
        s.rst(self.rst());
        s.syn(self.syn());
        s.fin(self.fin());
        s
    }
}

// NOTE(joshlf): In order to ensure that the checksum is always valid, we don't
// expose any setters for the fields of the TCP segment; the only way to set
// them is via TcpSegmentBuilder. This, combined with checksum validation
// performed in TcpSegment::parse, provides the invariant that a UdpPacket
// always has a valid checksum.

/// A builder for TCP segments.
#[derive(Copy, Clone, Debug)]
pub(crate) struct TcpSegmentBuilder<A: IpAddress> {
    src_ip: A,
    dst_ip: A,
    src_port: u16,
    dst_port: u16,
    seq_num: u32,
    ack_num: u32,
    flags: u16,
    window_size: u16,
}

impl<A: IpAddress> TcpSegmentBuilder<A> {
    /// Construct a new `TcpSegmentBuilder`.
    ///
    /// If `ack_num` is `Some`, then the ACK flag will be set.
    pub(crate) fn new(
        src_ip: A,
        dst_ip: A,
        src_port: NonZeroU16,
        dst_port: NonZeroU16,
        seq_num: u32,
        ack_num: Option<u32>,
        window_size: u16,
    ) -> TcpSegmentBuilder<A> {
        let flags = if ack_num.is_some() { 1 << 4 } else { 0 };
        TcpSegmentBuilder {
            src_ip,
            dst_ip,
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
    pub(crate) fn rst(&mut self, rst: bool) {
        self.set_flag(RST_MASK, rst);
    }

    /// Set the SYN flag.
    pub(crate) fn syn(&mut self, syn: bool) {
        self.set_flag(SYN_MASK, syn);
    }

    /// Set the FIN flag.
    pub(crate) fn fin(&mut self, fin: bool) {
        self.set_flag(FIN_MASK, fin);
    }
}

impl<A: IpAddress> PacketBuilder for TcpSegmentBuilder<A> {
    fn header_len(&self) -> usize {
        TCP_MIN_HDR_LEN
    }

    fn min_body_len(&self) -> usize {
        0
    }

    fn max_body_len(&self) -> usize {
        std::usize::MAX
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize(self, mut buffer: SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to
        // prevent leaking information from packets previously stored in
        // this buffer.
        let hdr_prefix =
            header.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for TCP header");
        // create a 0-byte slice for the options since we don't support
        // serializing options yet (NET-955)
        let options =
            Options::parse(&mut [][..]).expect("parsing an empty options slice should not fail");
        let mut segment = TcpSegment { hdr_prefix, options, body };

        NetworkEndian::write_u16(&mut segment.hdr_prefix.src_port, self.src_port);
        NetworkEndian::write_u16(&mut segment.hdr_prefix.dst_port, self.dst_port);
        NetworkEndian::write_u32(&mut segment.hdr_prefix.seq_num, self.seq_num);
        NetworkEndian::write_u32(&mut segment.hdr_prefix.ack, self.ack_num);
        // Data Offset is hard-coded to 5 until we support serializing
        // options.
        NetworkEndian::write_u16(
            &mut segment.hdr_prefix.data_offset_reserved_flags,
            (5u16 << 12) | self.flags,
        );
        NetworkEndian::write_u16(&mut segment.hdr_prefix.window_size, self.window_size);
        // We don't support setting the Urgent Pointer.
        NetworkEndian::write_u16(&mut segment.hdr_prefix.urg_ptr, 0);
        // Initialize the checksum to 0 so that we will get the correct
        // value when we compute it below.
        NetworkEndian::write_u16(&mut segment.hdr_prefix.checksum, 0);

        // NOTE: We stop using segment at this point so that it no longer
        // borrows the buffer, and we can use the buffer directly.
        let segment_len = buffer.as_ref().len();
        let checksum =
            compute_transport_checksum(self.src_ip, self.dst_ip, IpProto::Tcp, buffer.as_ref())
                .unwrap_or_else(|| {
                    panic!(
                    "total TCP segment length of {} bytes overflows length field of pseudo-header",
                    segment_len
                )
                });
        NetworkEndian::write_u16(&mut buffer.as_mut()[CHECKSUM_OFFSET..], checksum);
    }
}

const ACK_MASK: u16 = 0b10000;
const RST_MASK: u16 = 0b00100;
const SYN_MASK: u16 = 0b00010;
const FIN_MASK: u16 = 0b00001;

mod options {
    use byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::LayoutVerified;

    use crate::transport::tcp::TcpOption;
    use crate::wire::util::records::options::{OptionsImpl, OptionsImplLayout};

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;
    const OPTION_KIND_MSS: u8 = 2;
    const OPTION_KIND_WINDOW_SCALE: u8 = 3;
    const OPTION_KIND_SACK_PERMITTED: u8 = 4;
    const OPTION_KIND_SACK: u8 = 5;
    const OPTION_KIND_TIMESTAMP: u8 = 8;

    pub(crate) struct TcpOptionsImpl;

    impl OptionsImplLayout for TcpOptionsImpl {
        type Error = ();
    }

    impl<'a> OptionsImpl<'a> for TcpOptionsImpl {
        type Option = TcpOption<'a>;

        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<TcpOption>, ()> {
            match kind {
                self::OPTION_KIND_EOL | self::OPTION_KIND_NOP => {
                    unreachable!("wire::util::Options promises to handle EOL and NOP")
                }
                self::OPTION_KIND_MSS => {
                    if data.len() != 2 {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::Mss(NetworkEndian::read_u16(&data))))
                    }
                }
                self::OPTION_KIND_WINDOW_SCALE => {
                    if data.len() != 1 {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::WindowScale(data[0])))
                    }
                }
                self::OPTION_KIND_SACK_PERMITTED => {
                    if !data.is_empty() {
                        Err(())
                    } else {
                        Ok(Some(TcpOption::SackPermitted))
                    }
                }
                self::OPTION_KIND_SACK => Ok(Some(TcpOption::Sack(
                    LayoutVerified::new_slice(data).ok_or(())?.into_slice(),
                ))),
                self::OPTION_KIND_TIMESTAMP => {
                    if data.len() != 8 {
                        Err(())
                    } else {
                        let ts_val = NetworkEndian::read_u32(&data);
                        let ts_echo_reply = NetworkEndian::read_u32(&data[4..]);
                        Ok(Some(TcpOption::Timestamp { ts_val, ts_echo_reply }))
                    }
                }
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
    use packet::{Buf, BufferSerializer, ParseBuffer, Serializer};

    use super::*;
    use crate::device::ethernet::EtherType;
    use crate::ip::{IpProto, Ipv4Addr, Ipv6Addr};
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
        use crate::wire::testdata::tls_client_hello::*;

        let mut buf = &ETHERNET_FRAME_BYTES[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        assert_eq!(frame.src_mac(), ETHERNET_SRC_MAC);
        assert_eq!(frame.dst_mac(), ETHERNET_DST_MAC);
        assert_eq!(frame.ethertype(), Some(EtherType::Ipv4));

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        assert_eq!(packet.proto(), IpProto::Tcp);
        assert_eq!(packet.dscp(), IP_DSCP);
        assert_eq!(packet.ecn(), IP_ECN);
        assert_eq!(packet.df_flag(), IP_DONT_FRAGMENT);
        assert_eq!(packet.mf_flag(), IP_MORE_FRAGMENTS);
        assert_eq!(packet.fragment_offset(), IP_FRAGMENT_OFFSET);
        assert_eq!(packet.id(), IP_ID);
        assert_eq!(packet.ttl(), IP_TTL);
        assert_eq!(packet.src_ip(), IP_SRC_IP);
        assert_eq!(packet.dst_ip(), IP_DST_IP);

        let mut body = packet.body();
        let segment = body
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        assert_eq!(segment.src_port().get(), TCP_SRC_PORT);
        assert_eq!(segment.dst_port().get(), TCP_DST_PORT);
        assert_eq!(segment.ack_num().is_some(), TCP_ACK_FLAG);
        assert_eq!(segment.fin(), TCP_FIN_FLAG);
        assert_eq!(segment.syn(), TCP_SYN_FLAG);
        assert_eq!(segment.iter_options().collect::<Vec<_>>().as_slice(), TCP_OPTIONS);
        assert_eq!(segment.body(), TCP_BODY);

        // TODO(joshlf): Uncomment once we support serializing options
        // let buffer = segment.body()
        //     .encapsulate(segment.builder(packet.src_ip(), packet.dst_ip()))
        //     .encapsulate(packet.builder())
        //     .encapsulate(frame.builder())
        //     .serialize_outer().unwrap();
        // assert_eq!(buffer.as_ref(), ETHERNET_FRAME_BYTES);
    }

    fn hdr_prefix_to_bytes(hdr_prefix: HeaderPrefix) -> [u8; HDR_PREFIX_LEN] {
        let mut bytes = [0; HDR_PREFIX_LEN];
        {
            let mut lv = LayoutVerified::<_, HeaderPrefix>::new_unaligned(&mut bytes[..]).unwrap();
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
        let mut buf = &hdr_prefix_to_bytes(new_hdr_prefix())[..];
        let segment = buf
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        assert_eq!(segment.src_port().get(), 1);
        assert_eq!(segment.dst_port().get(), 2);
        assert_eq!(segment.body(), []);
    }

    #[test]
    fn test_parse_error() {
        // Assert that parsing a particular header prefix results in an error.
        // This function is responsible for ensuring that the checksum is
        // correct so that checksum errors won't hide the errors we're trying to
        // test.
        fn assert_header_err(hdr_prefix: HeaderPrefix, err: ParseError) {
            let mut buf = &mut hdr_prefix_to_bytes(hdr_prefix)[..];
            NetworkEndian::write_u16(&mut buf[CHECKSUM_OFFSET..], 0);
            let checksum =
                compute_transport_checksum(TEST_SRC_IPV4, TEST_DST_IPV4, IpProto::Tcp, buf)
                    .unwrap();
            NetworkEndian::write_u16(&mut buf[CHECKSUM_OFFSET..], checksum);
            assert_eq!(
                buf.parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
                    .unwrap_err(),
                err
            );
        }

        // Set the source port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.src_port = [0, 0];
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the destination port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.dst_port = [0, 0];
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 4, implying a header length of 16. This is
        // smaller than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        NetworkEndian::write_u16(&mut hdr_prefix.data_offset_reserved_flags, 4u16 << 12);
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 6, implying a header length of 24. This is
        // larger than the actual segment length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        NetworkEndian::write_u16(&mut hdr_prefix.data_offset_reserved_flags, 6u16 << 12);
        assert_header_err(hdr_prefix, ParseError::Format);
    }

    // Return a stock TcpSegmentBuilder with reasonable default values.
    fn new_builder<A: IpAddress>(src_ip: A, dst_ip: A) -> TcpSegmentBuilder<A> {
        TcpSegmentBuilder::new(
            src_ip,
            dst_ip,
            NonZeroU16::new(1).unwrap(),
            NonZeroU16::new(2).unwrap(),
            3,
            Some(4),
            5,
        )
    }

    #[test]
    fn test_serialize() {
        let mut builder = new_builder(TEST_SRC_IPV4, TEST_DST_IPV4);
        builder.fin(true);
        builder.rst(true);
        builder.syn(true);

        let mut buf =
            (&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9]).encapsulate(builder).serialize_outer().unwrap();
        // assert that we get the literal bytes we expected
        assert_eq!(
            buf.as_ref(),
            [
                0, 1, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 80, 23, 0, 5, 141, 137, 0, 0, 0, 1, 2, 3, 3, 4,
                5, 7, 8, 9
            ]
        );
        let segment = buf
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
            .unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
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
        let mut buf_0 = [0; HDR_PREFIX_LEN];
        BufferSerializer::new_vec(Buf::new(&mut buf_0[..], HDR_PREFIX_LEN..))
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_outer()
            .unwrap();
        let mut buf_1 = [0xFF; HDR_PREFIX_LEN];
        BufferSerializer::new_vec(Buf::new(&mut buf_1[..], HDR_PREFIX_LEN..))
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_outer()
            .unwrap();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_segment_too_long_ipv4() {
        // Test that a segment length which overflows u16 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        BufferSerializer::new_vec(Buf::new(&mut [0; (1 << 16) - HDR_PREFIX_LEN][..], ..))
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_outer()
            .unwrap();
    }

    #[test]
    #[ignore] // this test panics with stack overflow; TODO(joshlf): Fix
    #[should_panic]
    #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    fn test_serialize_panic_segment_too_long_ipv6() {
        // Test that a segment length which overflows u32 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        BufferSerializer::new_vec(Buf::new(&mut [0; (1 << 32) - HDR_PREFIX_LEN][..], ..))
            .encapsulate(new_builder(TEST_SRC_IPV6, TEST_DST_IPV6))
            .serialize_outer()
            .unwrap();
    }
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
        use crate::wire::testdata::tls_client_hello::*;
        let bytes = parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME_BYTES).unwrap().0;

        b.iter(|| {
            let mut buf = bytes;
            black_box(
                black_box(buf)
                    .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(IP_SRC_IP, IP_DST_IP))
                    .unwrap(),
            );
        })
    }

    #[bench]
    fn bench_serialize(b: &mut Bencher) {
        use crate::wire::testdata::tls_client_hello::*;

        let builder = TcpSegmentBuilder::new(
            IP_SRC_IP,
            IP_DST_IP,
            NonZeroU16::new(TCP_SRC_PORT).unwrap(),
            NonZeroU16::new(TCP_DST_PORT).unwrap(),
            0,
            None,
            0,
        );
        let mut buf = vec![0; builder.header_len() + TCP_BODY.len()];
        buf[builder.header_len()..].copy_from_slice(TCP_BODY);

        b.iter(|| {
            black_box(black_box((&mut buf[..]).encapsulate(builder.clone())).serialize_outer());
        })
    }
}
