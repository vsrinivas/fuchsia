// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv4 packets.

use std::fmt::{self, Debug, Formatter};

use byteorder::{ByteOrder, NetworkEndian};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::{ParseError, ParseResult};
use crate::ip::{IpProto, Ipv4Addr, Ipv4Option};
use crate::wire::util::checksum::Checksum;
use crate::wire::util::records::options::Options;

use self::options::Ipv4OptionsImpl;

const HDR_PREFIX_LEN: usize = 20;
pub(crate) const IPV4_MIN_HDR_LEN: usize = HDR_PREFIX_LEN;
pub(crate) const IPV4_MAX_HDR_LEN: usize = 60;
#[cfg(all(test, feature = "benchmark"))]
pub(crate) const IPV4_TTL_OFFSET: usize = 8;
#[cfg(all(test, feature = "benchmark"))]
pub(crate) const IPV4_CHECKSUM_OFFSET: usize = 10;

#[allow(missing_docs)]
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct HeaderPrefix {
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

impl HeaderPrefix {
    fn version(&self) -> u8 {
        self.version_ihl >> 4
    }

    /// Get the Internet Header Length (IHL).
    pub(crate) fn ihl(&self) -> u8 {
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
pub(crate) struct Ipv4Packet<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    options: Options<B, Ipv4OptionsImpl>,
    body: B,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv4Packet<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len() + self.options.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: ()) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/IPv4#Header

        let total_len = buffer.len();
        let hdr_prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let hdr_bytes = (hdr_prefix.ihl() * 4) as usize;
        if hdr_bytes < HDR_PREFIX_LEN {
            return debug_err!(Err(ParseError::Format), "invalid IHL: {}", hdr_prefix.ihl());
        }
        let options = buffer
            .take_front(hdr_bytes - HDR_PREFIX_LEN)
            .ok_or_else(debug_err_fn!(ParseError::Format, "IHL larger than buffer"))?;
        let options = Options::parse(options)
            .map_err(|err| debug_err!(ParseError::Format, "malformed options: {:?}", err))?;
        if hdr_prefix.version() != 4 {
            return debug_err!(
                Err(ParseError::Format),
                "unexpected IP version: {}",
                hdr_prefix.version()
            );
        }
        let body = if (hdr_prefix.total_length() as usize) < total_len {
            // Discard the padding left by the previous layer. This unwrap is
            // safe because of the check against total_len.
            buffer.take_back(total_len - (hdr_prefix.total_length() as usize)).unwrap();
            buffer.into_rest()
        } else if hdr_prefix.total_length() as usize == total_len {
            buffer.into_rest()
        } else {
            // we don't yet support IPv4 fragmentation
            return debug_err!(Err(ParseError::NotSupported), "fragmentation not supported");
        };

        let packet = Ipv4Packet { hdr_prefix, options, body };
        if packet.compute_header_checksum() != packet.hdr_prefix.hdr_checksum() {
            return debug_err!(Err(ParseError::Checksum), "invalid checksum");
        }
        Ok(packet)
    }
}

impl<B: ByteSlice> Ipv4Packet<B> {
    /// Iterate over the IPv4 header options.
    pub(crate) fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = Ipv4Option> {
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
    pub(crate) fn body(&self) -> &[u8] {
        &self.body
    }

    /// The Differentiated Services Code Point (DSCP).
    pub(crate) fn dscp(&self) -> u8 {
        self.hdr_prefix.dscp_ecn >> 2
    }

    /// The Explicit Congestion Notification (ECN).
    pub(crate) fn ecn(&self) -> u8 {
        self.hdr_prefix.dscp_ecn & 3
    }

    /// The identification.
    pub(crate) fn id(&self) -> u16 {
        NetworkEndian::read_u16(&self.hdr_prefix.id)
    }

    /// The Don't Fragment (DF) flag.
    pub(crate) fn df_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.hdr_prefix.flags_frag_off[0] & (1 << (5 + DF_FLAG_OFFSET)) > 0
    }

    /// The More Fragments (MF) flag.
    pub(crate) fn mf_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.hdr_prefix.flags_frag_off[0] & (1 << (5 + MF_FLAG_OFFSET)) > 0
    }

    /// The fragment offset.
    pub(crate) fn fragment_offset(&self) -> u16 {
        ((u16::from(self.hdr_prefix.flags_frag_off[0] & 0x1F)) << 8)
            | u16::from(self.hdr_prefix.flags_frag_off[1])
    }

    /// The Time To Live (TTL).
    pub(crate) fn ttl(&self) -> u8 {
        self.hdr_prefix.ttl
    }

    /// The IP Protocol.
    ///
    /// `proto` returns the `IpProto` from the protocol field.
    pub(crate) fn proto(&self) -> IpProto {
        IpProto::from(self.hdr_prefix.proto)
    }

    /// The source IP address.
    pub(crate) fn src_ip(&self) -> Ipv4Addr {
        Ipv4Addr::new(self.hdr_prefix.src_ip)
    }

    /// The destination IP address.
    pub(crate) fn dst_ip(&self) -> Ipv4Addr {
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

    /// Construct a builder with the same contents as this packet.
    pub(crate) fn builder(&self) -> Ipv4PacketBuilder {
        let mut s = Ipv4PacketBuilder {
            dscp: self.dscp(),
            ecn: self.ecn(),
            id: self.id(),
            flags: 0,
            frag_off: self.fragment_offset(),
            ttl: self.ttl(),
            proto: self.hdr_prefix.proto,
            src_ip: self.src_ip(),
            dst_ip: self.dst_ip(),
        };
        s.df_flag(self.df_flag());
        s.mf_flag(self.mf_flag());
        s
    }
}

impl<B> Ipv4Packet<B>
where
    B: ByteSliceMut,
{
    /// Set the Time To Live (TTL).
    ///
    /// Set the TTL and update the header checksum accordingly.
    pub(crate) fn set_ttl(&mut self, ttl: u8) {
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
#[derive(Debug)]
pub(crate) struct Ipv4PacketBuilder {
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
    /// Construct a new `Ipv4PacketBuilder`.
    pub(crate) fn new(
        src_ip: Ipv4Addr,
        dst_ip: Ipv4Addr,
        ttl: u8,
        proto: IpProto,
    ) -> Ipv4PacketBuilder {
        Ipv4PacketBuilder {
            dscp: 0,
            ecn: 0,
            id: 0,
            flags: 0,
            frag_off: 0,
            ttl,
            proto: proto.into(),
            src_ip,
            dst_ip,
        }
    }

    /// Set the Differentiated Services Code Point (DSCP).
    ///
    /// # Panics
    ///
    /// `dscp` panics if `dscp` is greater than 2^6 - 1.
    pub(crate) fn dscp(&mut self, dscp: u8) {
        assert!(dscp <= 1 << 6, "invalid DCSP: {}", dscp);
        self.dscp = dscp;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    pub(crate) fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 3, "invalid ECN: {}", ecn);
        self.ecn = ecn;
    }

    /// Set the identification.
    pub(crate) fn id(&mut self, id: u16) {
        self.id = id;
    }

    /// Set the Don't Fragment (DF) flag.
    pub(crate) fn df_flag(&mut self, df: bool) {
        if df {
            self.flags |= 1 << DF_FLAG_OFFSET;
        } else {
            self.flags &= !(1 << DF_FLAG_OFFSET);
        }
    }

    /// Set the More Fragments (MF) flag.
    pub(crate) fn mf_flag(&mut self, mf: bool) {
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
    pub(crate) fn fragment_offset(&mut self, fragment_offset: u16) {
        assert!(fragment_offset < 1 << 13, "invalid fragment offset: {}", fragment_offset);
        self.frag_off = fragment_offset;
    }
}

impl PacketBuilder for Ipv4PacketBuilder {
    fn header_len(&self) -> usize {
        IPV4_MIN_HDR_LEN
    }

    fn min_body_len(&self) -> usize {
        0
    }

    fn max_body_len(&self) -> usize {
        (1 << 16) - 1 - self.header_len()
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize(self, mut buffer: SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let hdr_prefix =
            header.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for IPv4 header");
        // create a 0-byte slice for the options since we don't support
        // serializing options yet (NET-955)
        let options =
            Options::parse(&mut [][..]).expect("parsing an empty options slice should not fail");
        let mut packet = Ipv4Packet { hdr_prefix, options, body };

        packet.hdr_prefix.version_ihl = (4u8 << 4) | 5;
        packet.hdr_prefix.dscp_ecn = (self.dscp << 2) | self.ecn;
        // The caller promises to supply a body whose length does not exceed
        // max_body_len. Doing this as a debug_assert (rather than an assert) is
        // fine because, with debug assertions disabled, we'll just write an
        // incorrect header value, which is acceptable if the caller has
        // violated their contract.
        debug_assert!(packet.total_packet_len() <= std::u16::MAX as usize);
        let total_len = packet.total_packet_len() as u16;
        NetworkEndian::write_u16(&mut packet.hdr_prefix.total_len, total_len);
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
    }
}

// bit positions into the flags bits
const DF_FLAG_OFFSET: u32 = 1;
const MF_FLAG_OFFSET: u32 = 0;

mod options {
    use crate::ip::{Ipv4Option, Ipv4OptionData};
    use crate::wire::util::records::options::{OptionsImpl, OptionsImplLayout};

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;

    pub(crate) struct Ipv4OptionsImpl;

    impl OptionsImplLayout for Ipv4OptionsImpl {
        type Error = ();
    }

    impl<'a> OptionsImpl<'a> for Ipv4OptionsImpl {
        type Option = Ipv4Option<'a>;

        fn parse(kind: u8, data: &[u8]) -> Result<Option<Ipv4Option>, ()> {
            let copied = kind & (1 << 7) > 0;
            match kind {
                self::OPTION_KIND_EOL | self::OPTION_KIND_NOP => {
                    unreachable!("wire::util::Options promises to handle EOL and NOP")
                }
                kind => {
                    if data.len() > 38 {
                        Err(())
                    } else {
                        Ok(Some(Ipv4Option {
                            copied,
                            data: Ipv4OptionData::Unrecognized {
                                kind,
                                len: data.len() as u8,
                                data,
                            },
                        }))
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, ParseBuffer, Serializer};

    use super::*;
    use crate::device::ethernet::{EtherType, Mac};
    use crate::ip::{IpExt, Ipv4};
    use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};

    const DEFAULT_SRC_MAC: Mac = Mac::new([1, 2, 3, 4, 5, 6]);
    const DEFAULT_DST_MAC: Mac = Mac::new([7, 8, 9, 0, 1, 2]);
    const DEFAULT_SRC_IP: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const DEFAULT_DST_IP: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);

    #[test]
    fn test_parse_serialize_full_tcp() {
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

        let buffer = packet
            .body()
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME_BYTES);
    }

    #[test]
    fn test_parse_serialize_full_udp() {
        use crate::wire::testdata::dns_request::*;

        let mut buf = &ETHERNET_FRAME_BYTES[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        assert_eq!(frame.src_mac(), ETHERNET_SRC_MAC);
        assert_eq!(frame.dst_mac(), ETHERNET_DST_MAC);
        assert_eq!(frame.ethertype(), Some(EtherType::Ipv4));

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        assert_eq!(packet.proto(), IpProto::Udp);
        assert_eq!(packet.dscp(), IP_DSCP);
        assert_eq!(packet.ecn(), IP_ECN);
        assert_eq!(packet.df_flag(), IP_DONT_FRAGMENT);
        assert_eq!(packet.mf_flag(), IP_MORE_FRAGMENTS);
        assert_eq!(packet.fragment_offset(), IP_FRAGMENT_OFFSET);
        assert_eq!(packet.id(), IP_ID);
        assert_eq!(packet.ttl(), IP_TTL);
        assert_eq!(packet.src_ip(), IP_SRC_IP);
        assert_eq!(packet.dst_ip(), IP_DST_IP);

        let buffer = packet
            .body()
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME_BYTES);
    }

    fn hdr_prefix_to_bytes(hdr_prefix: HeaderPrefix) -> [u8; 20] {
        let mut bytes = [0; 20];
        {
            let mut lv = LayoutVerified::<_, HeaderPrefix>::new_unaligned(&mut bytes[..]).unwrap();
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
        hdr_prefix.proto = IpProto::Tcp.into();
        hdr_prefix.src_ip = DEFAULT_SRC_IP.ipv4_bytes();
        hdr_prefix.dst_ip = DEFAULT_DST_IP.ipv4_bytes();
        hdr_prefix.hdr_checksum = [0xa6, 0xcf];
        hdr_prefix
    }

    #[test]
    fn test_parse() {
        let mut bytes = &hdr_prefix_to_bytes(new_hdr_prefix())[..];
        let packet = bytes.parse::<Ipv4Packet<_>>().unwrap();
        assert_eq!(packet.id(), 0x0102);
        assert_eq!(packet.ttl(), 0x03);
        assert_eq!(packet.proto(), IpProto::Tcp);
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), []);
    }

    fn test_parse_padding() {
        // Test that we properly discard post-packet padding.
        let mut buffer = BufferSerializer::new_vec(Buf::new(vec![], ..))
            .encapsulate(<Ipv4 as IpExt<&[u8]>>::PacketBuilder::new(
                DEFAULT_DST_IP,
                DEFAULT_DST_IP,
                0,
                IpProto::Tcp,
            ))
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_SRC_MAC,
                DEFAULT_DST_MAC,
                EtherType::Ipv4,
            ))
            .serialize_outer()
            .unwrap();
        buffer.parse::<EthernetFrame<_>>().unwrap();
        // Test that the Ethernet body is the minimum length, which far exceeds
        // the IPv4 packet header size of 20 bytes (without options).
        assert_eq!(buffer.len(), 46);
        let packet = buffer.parse::<Ipv4Packet<_>>().unwrap();
        // Test that we've properly discarded the post-packet padding, and have
        // an empty body.
        assert_eq!(packet.body().len(), 0);
        // Test that we not only ignored the padding, but properly consumed it
        // from the underlying buffer as we're required to do by the
        // ParsablePacket contract.
        assert_eq!(buffer.len(), 0);
    }

    #[test]
    fn test_parse_error() {
        // Set the version to 5. The version must be 4.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (5 << 4) | 5;
        assert_eq!(
            (&hdr_prefix_to_bytes(hdr_prefix)[..]).parse::<Ipv4Packet<_>>().unwrap_err(),
            ParseError::Format
        );

        // Set the IHL to 4, implying a header length of 16. This is smaller
        // than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 4;
        assert_eq!(
            (&hdr_prefix_to_bytes(hdr_prefix)[..]).parse::<Ipv4Packet<_>>().unwrap_err(),
            ParseError::Format
        );

        // Set the IHL to 6, implying a header length of 24. This is larger than
        // the actual packet length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 6;
        assert_eq!(
            (&hdr_prefix_to_bytes(hdr_prefix)[..]).parse::<Ipv4Packet<_>>().unwrap_err(),
            ParseError::Format
        );
    }

    // Return a stock Ipv4PacketBuilder with reasonable default values.
    fn new_builder() -> Ipv4PacketBuilder {
        Ipv4PacketBuilder::new(DEFAULT_DST_IP, DEFAULT_DST_IP, 64, IpProto::Tcp)
    }

    #[test]
    fn test_serialize() {
        let mut builder = new_builder();
        builder.dscp(0x12);
        builder.ecn(3);
        builder.id(0x0405);
        builder.df_flag(true);
        builder.mf_flag(true);
        builder.fragment_offset(0x0607);

        let mut buf =
            (&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9]).encapsulate(builder).serialize_outer().unwrap();
        assert_eq!(
            buf.as_ref(),
            [
                69, 75, 0, 30, 4, 5, 102, 7, 64, 6, 248, 103, 5, 6, 7, 8, 5, 6, 7, 8, 0, 1, 2, 3,
                3, 4, 5, 7, 8, 9
            ],
        );
        let packet = buf.parse::<Ipv4Packet<_>>().unwrap();
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
        let mut buf_0 = [0; IPV4_MIN_HDR_LEN];
        BufferSerializer::new_vec(Buf::new(&mut buf_0[..], IPV4_MIN_HDR_LEN..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
        let mut buf_1 = [0xFF; IPV4_MIN_HDR_LEN];
        BufferSerializer::new_vec(Buf::new(&mut buf_1[..], IPV4_MIN_HDR_LEN..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_packet_length() {
        // Test that a packet which is longer than 2^16 - 1 bytes is rejected.
        BufferSerializer::new_vec(Buf::new(&mut [0; (1 << 16) - IPV4_MIN_HDR_LEN][..], ..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
    }
}
