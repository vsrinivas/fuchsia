// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of TCP segments.

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::num::NonZeroU16;
use std::ops::Range;

use net_types::ip::IpAddress;
use packet::{
    BufferView, BufferViewMut, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
    SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::IpProto;
#[cfg(test)]
use crate::transport::tcp::TcpOption;
use crate::wire::records::options::{Options, OptionsRaw};
use crate::wire::{compute_transport_checksum_parts, compute_transport_checksum_serialize};
use crate::wire::{FromRaw, MaybeParsed, U16, U32};

use self::options::TcpOptionsImpl;

const HDR_PREFIX_LEN: usize = 20;
const CHECKSUM_OFFSET: usize = 16;
const CHECKSUM_RANGE: Range<usize> = CHECKSUM_OFFSET..CHECKSUM_OFFSET + 2;
pub(crate) const TCP_MIN_HDR_LEN: usize = HDR_PREFIX_LEN;

#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct HeaderPrefix {
    src_port: U16,
    dst_port: U16,
    seq_num: U32,
    ack: U32,
    data_offset_reserved_flags: U16,
    window_size: U16,
    checksum: [u8; 2],
    urg_ptr: U16,
}

impl HeaderPrefix {
    fn data_offset(&self) -> u8 {
        (self.data_offset_reserved_flags.get() >> 12) as u8
    }

    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    fn set_data_offset(&mut self, offset: u8) {
        debug_assert!(offset <= 15);
        let v = self.data_offset_reserved_flags.get();
        self.data_offset_reserved_flags.set((v & 0x0FFF) | (((offset & 0x0F) as u16) << 12));
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
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
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

    fn parse<BV: BufferView<B>>(buffer: BV, args: TcpParseArgs<A>) -> ParseResult<Self> {
        TcpSegmentRaw::<B>::parse(buffer, ()).and_then(|u| TcpSegment::try_from_raw_with(u, args))
    }
}

impl<B: ByteSlice, A: IpAddress> FromRaw<TcpSegmentRaw<B>, TcpParseArgs<A>> for TcpSegment<B> {
    type Error = ParseError;

    fn try_from_raw_with(
        raw: TcpSegmentRaw<B>,
        args: TcpParseArgs<A>,
    ) -> Result<Self, Self::Error> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let hdr_prefix = raw
            .hdr_prefix
            .ok_or_else(|_| debug_err!(ParseError::Format, "too few bytes for header"))?;
        let options = raw
            .options
            .ok_or_else(|_| debug_err!(ParseError::Format, "Incomplete options"))
            .and_then(|o| {
                Options::try_from_raw(o)
                    .map_err(|_| debug_err!(ParseError::Format, "Options validation failed"))
            })?;
        let body = raw.body;

        let hdr_bytes = (hdr_prefix.data_offset() * 4) as usize;
        if hdr_bytes != hdr_prefix.bytes().len() + options.bytes().len() {
            return debug_err!(
                Err(ParseError::Format),
                "invalid data offset: {} for header={} + options={}",
                hdr_prefix.data_offset(),
                hdr_prefix.bytes().len(),
                options.bytes().len()
            );
        }

        let parts = [hdr_prefix.bytes(), options.bytes(), body.deref().as_ref()];
        let checksum =
            compute_transport_checksum_parts(args.src_ip, args.dst_ip, IpProto::Tcp, parts.iter())
                .ok_or_else(debug_err_fn!(ParseError::Format, "segment too large"))?;

        if checksum != [0, 0] {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }

        if hdr_prefix.src_port == U16::ZERO || hdr_prefix.dst_port == U16::ZERO {
            return debug_err!(Err(ParseError::Format), "zero source or destination port");
        }

        Ok(TcpSegment { hdr_prefix, options, body })
    }
}

impl<B: ByteSlice> TcpSegment<B> {
    /// Iterate over the TCP header options.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = TcpOption> {
        self.options.iter()
    }

    /// The segment body.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn body(&self) -> &[u8] {
        &self.body
    }

    /// The source port.
    pub(crate) fn src_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.src_port.get()).unwrap()
    }

    /// The destination port.
    pub(crate) fn dst_port(&self) -> NonZeroU16 {
        // Infallible because this was already validated in parse
        NonZeroU16::new(self.hdr_prefix.dst_port.get()).unwrap()
    }

    /// The sequence number.
    pub(crate) fn seq_num(&self) -> u32 {
        self.hdr_prefix.seq_num.get()
    }

    /// The acknowledgement number.
    ///
    /// If the ACK flag is not set, `ack_num` returns `None`.
    #[cfg(test)]
    pub(crate) fn ack_num(&self) -> Option<u32> {
        if self.get_flag(ACK_MASK) {
            Some(self.hdr_prefix.ack.get())
        } else {
            None
        }
    }

    fn get_flag(&self, mask: u16) -> bool {
        self.hdr_prefix.data_offset_reserved_flags.get() & mask > 0
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
        self.hdr_prefix.window_size.get()
    }

    // The length of the header prefix and options.
    pub(crate) fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len() + self.options.bytes().len()
    }

    // The length of the segment as calculated from the header prefix, options,
    // and body.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    fn total_segment_len(&self) -> usize {
        self.header_len() + self.body.len()
    }

    /// Construct a builder with the same contents as this packet.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    pub(crate) fn builder<A: IpAddress>(&self, src_ip: A, dst_ip: A) -> TcpSegmentBuilder<A> {
        let mut s = TcpSegmentBuilder {
            src_ip,
            dst_ip,
            src_port: self.src_port().get(),
            dst_port: self.dst_port().get(),
            seq_num: self.seq_num(),
            ack_num: self.hdr_prefix.ack.get(),
            flags: 0,
            window_size: self.window_size(),
        };
        s.rst(self.rst());
        s.syn(self.syn());
        s.fin(self.fin());
        s
    }
}

/// The minimal information required from a TCP segment header.
///
/// A `TcpFlowHeader` may be the result of a partially parsed TCP segment in
/// [`TcpSegmentRaw`].
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct TcpFlowHeader {
    src_port: U16,
    dst_port: U16,
}

struct PartialHeaderPrefix<B> {
    flow: LayoutVerified<B, TcpFlowHeader>,
    rest: B,
}

/// A partially-parsed and not yet validated TCP segment.
///
/// A `TcpSegmentRaw` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
///
/// Parsing a `TcpSegmentRaw` from raw data will succeed as long as at least 4
/// bytes are available, which will be extracted as a [`TcpFlowHeader`] that
/// contains the TCP source and destination ports. A `TcpSegmentRaw` is, then,
/// guaranteed to always have at least that minimal information available.
///
/// [`TcpSegment`] provides a [`FromRaw`] implementation that can be used to
/// validate a `TcpSegmentRaw`.
pub(crate) struct TcpSegmentRaw<B> {
    hdr_prefix: MaybeParsed<LayoutVerified<B, HeaderPrefix>, PartialHeaderPrefix<B>>,
    options: MaybeParsed<OptionsRaw<B, TcpOptionsImpl>, B>,
    body: B,
}

impl<B> ParsablePacket<B, ()> for TcpSegmentRaw<B>
where
    B: ByteSlice,
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.options.len()
            + match &self.hdr_prefix {
                MaybeParsed::Complete(h) => h.bytes().len(),
                MaybeParsed::Incomplete(h) => h.flow.bytes().len() + h.rest.len(),
            };
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure

        let (hdr_prefix, options) = if let Some(pfx) = buffer.take_obj_front::<HeaderPrefix>() {
            // If the subtraction data_offset*4 - HDR_PREFIX_LEN would have been
            // negative, that would imply that data_offset has an invalid value.
            // Even though this will end up being MaybeParsed::Complete, the
            // data_offset value is validated when transforming TcpSegmentRaw to
            // TcpSegment.
            let option_bytes = ((pfx.data_offset() * 4) as usize).saturating_sub(HDR_PREFIX_LEN);
            let options =
                MaybeParsed::take_from_buffer_with(&mut buffer, option_bytes, OptionsRaw::new);
            let hdr_prefix = MaybeParsed::Complete(pfx);
            (hdr_prefix, options)
        } else {
            let flow = buffer
                .take_obj_front::<TcpFlowHeader>()
                .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for flow header"))?;
            let rest = buffer.take_rest_front();
            // if we can't take the entire header, the rest of options will be
            // incomplete:
            let hdr_prefix = MaybeParsed::Incomplete(PartialHeaderPrefix { flow, rest });
            let options = MaybeParsed::Incomplete(buffer.take_rest_front());
            (hdr_prefix, options)
        };

        // A TCP segment's body is always just the rest of the buffer:
        let body = buffer.into_rest();

        Ok(Self { hdr_prefix, options, body })
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
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
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
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(TCP_MIN_HDR_LEN, 0, 0, core::usize::MAX)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer) {
        let mut header = buffer.header();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to
        // prevent leaking information from packets previously stored in
        // this buffer.
        let mut hdr_prefix =
            header.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for TCP header");

        hdr_prefix.src_port = U16::new(self.src_port);
        hdr_prefix.dst_port = U16::new(self.dst_port);
        hdr_prefix.seq_num = U32::new(self.seq_num);
        hdr_prefix.ack = U32::new(self.ack_num);
        // Data Offset is hard-coded to 5 until we support serializing
        // options.
        hdr_prefix.data_offset_reserved_flags = U16::new((5u16 << 12) | self.flags);
        hdr_prefix.window_size = U16::new(self.window_size);
        // We don't support setting the Urgent Pointer.
        hdr_prefix.urg_ptr = U16::ZERO;
        // Initialize the checksum to 0 so that we will get the correct
        // value when we compute it below.
        hdr_prefix.checksum = [0, 0];

        #[rustfmt::skip]
        let checksum = compute_transport_checksum_serialize(
            self.src_ip,
            self.dst_ip,
            IpProto::Tcp,
            buffer,
        )
        .unwrap_or_else(|| {
            panic!(
                "total TCP segment length of {} bytes overflows length field of pseudo-header",
                buffer.len()
            )
        });
        buffer.header()[CHECKSUM_RANGE].copy_from_slice(&checksum[..]);
    }
}

#[cfg(test)]
const ACK_MASK: u16 = 0b10000;
const RST_MASK: u16 = 0b00100;
const SYN_MASK: u16 = 0b00010;
const FIN_MASK: u16 = 0b00001;

mod options {
    use byteorder::{ByteOrder, NetworkEndian};
    use zerocopy::LayoutVerified;

    use crate::transport::tcp::TcpOption;
    use crate::wire::records::options::{OptionsImpl, OptionsImplLayout};

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
                    unreachable!("wire::records::options::Options promises to handle EOL and NOP")
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
    fn fmt(&self, fmt: &mut Formatter<'_>) -> fmt::Result {
        write!(fmt, "TcpSegment")
    }
}

#[cfg(test)]
mod tests {
    use byteorder::{ByteOrder, NetworkEndian};
    use net_types::ip::{Ipv4, Ipv4Addr, Ipv6Addr};
    use packet::{Buf, InnerPacketBuilder, ParseBuffer, Serializer};
    use std::num::NonZeroU16;

    use super::*;
    use crate::ip::IpProto;
    use crate::testutil::benchmarks::{black_box, Bencher};
    use crate::testutil::*;
    use crate::wire::compute_transport_checksum;
    use crate::wire::ethernet::EthernetFrame;
    use crate::wire::ipv4::{Ipv4Header, Ipv4Packet};
    use crate::wire::ipv6::Ipv6Packet;

    const TEST_SRC_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_DST_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SRC_IPV6: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const TEST_DST_IPV6: Ipv6Addr =
        Ipv6Addr::new([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    #[test]
    fn test_parse_serialize_full_ipv4() {
        use crate::wire::testdata::tls_client_hello_v4::*;

        let mut buf = &ETHERNET_FRAME.bytes[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        verify_ipv4_packet(&packet, IPV4_PACKET);

        let mut body = packet.body();
        let segment = body
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);

        // TODO(joshlf): Uncomment once we support serializing options
        // let buffer = segment.body()
        //     .encapsulate(segment.builder(packet.src_ip(), packet.dst_ip()))
        //     .encapsulate(packet.builder())
        //     .encapsulate(frame.builder())
        //     .serialize_vec_outer().unwrap();
        // assert_eq!(buffer.as_ref(), ETHERNET_FRAME_BYTES);
    }

    #[test]
    fn test_parse_serialize_full_ipv6() {
        use crate::wire::testdata::syn_v6::*;

        let mut buf = &ETHERNET_FRAME.bytes[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv6Packet<_>>().unwrap();
        verify_ipv6_packet(&packet, IPV6_PACKET);

        let mut body = packet.body();
        let segment = body
            .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(packet.src_ip(), packet.dst_ip()))
            .unwrap();
        verify_tcp_segment(&segment, TCP_SEGMENT);

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
        hdr_prefix.src_port = U16::new(1);
        hdr_prefix.dst_port = U16::new(2);
        // data offset of 5
        hdr_prefix.data_offset_reserved_flags = U16::new(5u16 << 12);
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
            buf[CHECKSUM_RANGE].copy_from_slice(&checksum[..]);
            assert_eq!(
                buf.parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4))
                    .unwrap_err(),
                err
            );
        }

        // Set the source port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.src_port = U16::ZERO;
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the destination port to 0, which is illegal.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.dst_port = U16::ZERO;
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 4, implying a header length of 16. This is
        // smaller than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.data_offset_reserved_flags = U16::new(4u16 << 12);
        assert_header_err(hdr_prefix, ParseError::Format);

        // Set the data offset to 6, implying a header length of 24. This is
        // larger than the actual segment length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.data_offset_reserved_flags = U16::new(6u16 << 12);
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

        let mut buf = (&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9])
            .into_serializer()
            .encapsulate(builder)
            .serialize_vec_outer()
            .unwrap();
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
        Buf::new(&mut buf_0[..], HDR_PREFIX_LEN..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap();
        let mut buf_1 = [0xFF; HDR_PREFIX_LEN];
        Buf::new(&mut buf_1[..], HDR_PREFIX_LEN..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    #[should_panic(
        expected = "total TCP segment length of 65536 bytes overflows length field of pseudo-header"
    )]
    fn test_serialize_panic_segment_too_long_ipv4() {
        // Test that a segment length which overflows u16 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        Buf::new(&mut [0; (1 << 16) - HDR_PREFIX_LEN][..], ..)
            .encapsulate(new_builder(TEST_SRC_IPV4, TEST_DST_IPV4))
            .serialize_vec_outer()
            .unwrap();
    }

    #[test]
    #[ignore] // this test panics with stack overflow; TODO(joshlf): Fix
    #[cfg(target_pointer_width = "64")] // 2^32 overflows on 32-bit platforms
    fn test_serialize_panic_segment_too_long_ipv6() {
        // Test that a segment length which overflows u32 is rejected because it
        // can't fit in the length field in the IPv4 pseudo-header.
        Buf::new(&mut [0; (1 << 32) - HDR_PREFIX_LEN][..], ..)
            .encapsulate(new_builder(TEST_SRC_IPV6, TEST_DST_IPV6))
            .serialize_vec_outer()
            .unwrap();
    }

    #[test]
    fn test_partial_parse() {
        // Parse options partially:
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.set_data_offset(8);
        let mut bytes = hdr_prefix_to_bytes(hdr_prefix)[..].to_owned();
        bytes.extend(&[1, 2, 3, 4, 5]);
        let mut buf = &bytes[..];
        let packet = buf.parse::<TcpSegmentRaw<_>>().unwrap();
        assert_eq!(packet.hdr_prefix.as_ref().unwrap().bytes(), &bytes[0..20]);
        assert_eq!(packet.options.as_ref().unwrap_incomplete().len(), 5);
        assert_eq!(packet.body.len(), 0);
        // validation should fail:
        assert!(TcpSegment::try_from_raw_with(
            packet,
            TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4),
        )
        .is_err());

        // Parse header partially:
        let hdr_prefix = new_hdr_prefix();
        let bytes = hdr_prefix_to_bytes(hdr_prefix);
        let mut buf = &bytes[0..10];
        let packet = buf.parse::<TcpSegmentRaw<_>>().unwrap();
        let partial = packet.hdr_prefix.as_ref().unwrap_incomplete();
        assert_eq!(partial.flow.src_port.get(), 1);
        assert_eq!(partial.flow.dst_port.get(), 2);
        assert_eq!(partial.rest.len(), 6);
        assert!(packet.options.is_incomplete());
        assert_eq!(packet.body.len(), 0);
        // validation should fail:
        assert!(TcpSegment::try_from_raw_with(
            packet,
            TcpParseArgs::new(TEST_SRC_IPV4, TEST_DST_IPV4),
        )
        .is_err());

        let hdr_prefix = new_hdr_prefix();
        let bytes = hdr_prefix_to_bytes(hdr_prefix);
        // If we don't even have enough header bytes, we should fail partial
        // parsing:
        let mut buf = &bytes[0..3];
        assert!(buf.parse::<TcpSegmentRaw<_>>().is_err());
        // If we don't even have exactly 4 header bytes, we should succeed
        // partial parsing:
        let mut buf = &bytes[0..4];
        assert!(buf.parse::<TcpSegmentRaw<_>>().is_ok());
    }

    //
    // Benchmarks
    //

    fn bench_parse_inner<B: Bencher>(b: &mut B) {
        use crate::wire::testdata::tls_client_hello_v4::*;
        let bytes = parse_ip_packet_in_ethernet_frame::<Ipv4>(ETHERNET_FRAME.bytes).unwrap().0;

        b.iter(|| {
            let buf = bytes;
            black_box(
                black_box(buf)
                    .parse_with::<_, TcpSegment<_>>(TcpParseArgs::new(
                        IPV4_PACKET.metadata.src_ip,
                        IPV4_PACKET.metadata.dst_ip,
                    ))
                    .unwrap(),
            );
        })
    }

    bench!(bench_parse, bench_parse_inner);

    fn bench_serialize_inner<B: Bencher>(b: &mut B) {
        use crate::wire::testdata::tls_client_hello_v4::*;

        let builder = TcpSegmentBuilder::new(
            IPV4_PACKET.metadata.src_ip,
            IPV4_PACKET.metadata.dst_ip,
            NonZeroU16::new(TCP_SEGMENT.metadata.src_port).unwrap(),
            NonZeroU16::new(TCP_SEGMENT.metadata.dst_port).unwrap(),
            0,
            None,
            0,
        );

        let header_len = builder.constraints().header_len();
        let total_len = header_len + TCP_SEGMENT.bytes[TCP_SEGMENT.body_range].len();
        let mut buf = vec![0; total_len];
        buf[header_len..].copy_from_slice(&TCP_SEGMENT.bytes[TCP_SEGMENT.body_range]);

        b.iter(|| {
            black_box(
                black_box(
                    Buf::new(&mut buf[..], header_len..total_len).encapsulate(builder.clone()),
                )
                .serialize_no_alloc_outer(),
            )
            .unwrap();
        })
    }

    bench!(bench_serialize, bench_serialize_inner);
}
