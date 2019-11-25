// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv4 packets.

use std::convert::TryFrom;
use std::fmt::{self, Debug, Formatter};
use std::ops::Range;

use byteorder::{ByteOrder, NetworkEndian};
use internet_checksum::Checksum;
use net_types::ip::{Ipv4, Ipv4Addr};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
    SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::{IpParseError, IpParseResult, ParseError};
use crate::ip::reassembly::FragmentablePacket;
use crate::ip::{IpProto, Ipv4Option};
use crate::wire::records::options::OptionsRaw;
use crate::wire::U16;
use crate::wire::{FromRaw, MaybeParsed};

use self::options::Ipv4OptionsImpl;

const HDR_PREFIX_LEN: usize = 20;

/// The minimum length of an IPv4 header.
pub(crate) const IPV4_MIN_HDR_LEN: usize = HDR_PREFIX_LEN;

/// The maximum length of an IPv4 header.
pub(crate) const IPV4_MAX_HDR_LEN: usize = 60;

/// The maximum length for options in an IPv4 header.
pub(crate) const MAX_OPTIONS_LEN: usize = IPV4_MAX_HDR_LEN - HDR_PREFIX_LEN;

/// The range of bytes within an IPv4 header buffer that the
/// total length field uses.
pub(crate) const IPV4_TOTAL_LENGTH_BYTE_RANGE: Range<usize> = 2..4;

/// The range of bytes within an IPv4 header buffer that the
/// fragment data fields use.
pub(crate) const IPV4_FRAGMENT_DATA_BYTE_RANGE: Range<usize> = 4..8;

/// The range of bytes within an IPv4 header buffer that the
/// checksum field uses.
pub(crate) const IPV4_CHECKSUM_BYTE_RANGE: Range<usize> = 10..12;

#[cfg(test)]
pub(crate) const IPV4_TTL_OFFSET: usize = 8;
#[cfg(test)]
pub(crate) const IPV4_CHECKSUM_OFFSET: usize = 10;

#[allow(missing_docs)]
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct HeaderPrefix {
    version_ihl: u8,
    dscp_ecn: u8,
    total_len: U16,
    id: U16,
    flags_frag_off: [u8; 2],
    ttl: u8,
    proto: u8,
    hdr_checksum: [u8; 2],
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
}

impl HeaderPrefix {
    fn version(&self) -> u8 {
        self.version_ihl >> 4
    }

    /// Get the Internet Header Length (IHL).
    pub(crate) fn ihl(&self) -> u8 {
        self.version_ihl & 0xF
    }

    /// The More Fragments (MF) flag.
    pub(crate) fn mf_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.flags_frag_off[0] & (1 << (5 + MF_FLAG_OFFSET)) > 0
    }
}

/// Provides common access to IPv4 header fields.
///
/// `Ipv4Header` provides access to IPv4 header fields as a common
/// implementation for both [`Ipv4Packet`] and [`Ipv4PacketRaw`]
pub(crate) trait Ipv4Header {
    /// Gets a reference to the IPv4 [`HeaderPrefix`].
    fn get_header_prefix(&self) -> &HeaderPrefix;

    /// The Differentiated Services Code Point (DSCP).
    fn dscp(&self) -> u8 {
        self.get_header_prefix().dscp_ecn >> 2
    }

    /// The Explicit Congestion Notification (ECN).
    fn ecn(&self) -> u8 {
        self.get_header_prefix().dscp_ecn & 3
    }

    /// The identification.
    fn id(&self) -> u16 {
        self.get_header_prefix().id.get()
    }

    /// The Don't Fragment (DF) flag.
    fn df_flag(&self) -> bool {
        // the flags are the top 3 bits, so we need to shift by an extra 5 bits
        self.get_header_prefix().flags_frag_off[0] & (1 << (5 + DF_FLAG_OFFSET)) > 0
    }

    /// The More Fragments (MF) flag.
    fn mf_flag(&self) -> bool {
        self.get_header_prefix().mf_flag()
    }

    /// The fragment offset.
    fn fragment_offset(&self) -> u16 {
        ((u16::from(self.get_header_prefix().flags_frag_off[0] & 0x1F)) << 8)
            | u16::from(self.get_header_prefix().flags_frag_off[1])
    }

    /// The Time To Live (TTL).
    fn ttl(&self) -> u8 {
        self.get_header_prefix().ttl
    }

    /// The IP Protocol.
    ///
    /// `proto` returns the `IpProto` from the protocol field.
    fn proto(&self) -> IpProto {
        IpProto::from(self.get_header_prefix().proto)
    }

    /// The source IP address.
    fn src_ip(&self) -> Ipv4Addr {
        self.get_header_prefix().src_ip
    }

    /// The destination IP address.
    fn dst_ip(&self) -> Ipv4Addr {
        self.get_header_prefix().dst_ip
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
    options: Options<B>,
    body: B,
}

impl<B: ByteSlice> Ipv4Header for Ipv4Packet<B> {
    fn get_header_prefix(&self) -> &HeaderPrefix {
        &self.hdr_prefix
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv4Packet<B> {
    type Error = IpParseError<Ipv4>;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len() + self.options.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, args: ()) -> IpParseResult<Ipv4, Self> {
        Ipv4PacketRaw::<B>::parse(buffer, args).and_then(|u| Ipv4Packet::try_from_raw(u))
    }
}

impl<B: ByteSlice> FromRaw<Ipv4PacketRaw<B>, ()> for Ipv4Packet<B> {
    type Error = IpParseError<Ipv4>;

    fn try_from_raw_with(raw: Ipv4PacketRaw<B>, _args: ()) -> Result<Self, Self::Error> {
        let hdr_prefix = raw.hdr_prefix;
        let hdr_bytes = (hdr_prefix.ihl() * 4) as usize;

        if hdr_bytes < HDR_PREFIX_LEN {
            return debug_err!(Err(ParseError::Format.into()), "invalid IHL: {}", hdr_prefix.ihl());
        }

        let options = match raw.options {
            MaybeParsed::Incomplete(_) => {
                return debug_err!(Err(ParseError::Format.into()), "Incomplete options");
            }
            MaybeParsed::Complete(unchecked) => Options::try_from_raw(unchecked)
                .map_err(|e| debug_err!(ParseError::Format, "malformed options: {:?}", e))?,
        };

        if hdr_prefix.version() != 4 {
            return debug_err!(
                Err(ParseError::Format.into()),
                "unexpected IP version: {}",
                hdr_prefix.version()
            );
        }

        let body = match raw.body {
            MaybeParsed::Incomplete(_) => {
                if hdr_prefix.mf_flag() {
                    return debug_err!(
                        Err(ParseError::NotSupported.into()),
                        "fragmentation not supported"
                    );
                } else {
                    return debug_err!(Err(ParseError::Format.into()), "Incomplete body");
                }
            }
            MaybeParsed::Complete(bytes) => bytes,
        };

        let packet = Ipv4Packet { hdr_prefix, options, body };
        if packet.compute_header_checksum() != [0, 0] {
            return debug_err!(Err(ParseError::Checksum.into()), "invalid checksum");
        }
        Ok(packet)
    }
}

fn compute_header_checksum(hdr_prefix: &[u8], options: &[u8]) -> [u8; 2] {
    let mut c = Checksum::new();
    c.add_bytes(hdr_prefix);
    c.add_bytes(options);
    c.checksum()
}

impl<B: ByteSlice> Ipv4Packet<B> {
    /// Iterate over the IPv4 header options.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn iter_options<'a>(&'a self) -> impl 'a + Iterator<Item = Ipv4Option> {
        self.options.iter()
    }

    // Compute the header checksum, skipping the checksum field itself.
    fn compute_header_checksum(&self) -> [u8; 2] {
        compute_header_checksum(self.hdr_prefix.bytes(), self.options.bytes())
    }

    /// The packet body.
    pub(crate) fn body(&self) -> &[u8] {
        &self.body
    }

    /// The size of the header prefix and options.
    pub(crate) fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len() + self.options.bytes().len()
    }

    /// Return a buffer that is a copy of the header bytes in this
    /// packet, but patched to be not fragmented.
    ///
    /// Return a buffer of this packet's header and options with
    /// the fragment data zeroed out.
    pub(crate) fn copy_header_bytes_for_fragment(&self) -> Vec<u8> {
        let expected_bytes_len = self.header_len();
        let mut bytes = Vec::with_capacity(expected_bytes_len);

        bytes.extend_from_slice(self.hdr_prefix.bytes());
        bytes.extend_from_slice(self.options.bytes());

        // `bytes`'s length should be exactly `expected_bytes_len`.
        assert_eq!(bytes.len(), expected_bytes_len);

        // Zero out the fragment data.
        bytes[IPV4_FRAGMENT_DATA_BYTE_RANGE].copy_from_slice(&[0; 4][..]);

        bytes
    }

    /// Construct a builder with the same contents as this packet.
    #[cfg(test)]
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

impl<B: ByteSlice> FragmentablePacket for Ipv4Packet<B> {
    fn fragment_data(&self) -> (u32, u16, bool) {
        (u32::from(self.id()), self.fragment_offset(), self.mf_flag())
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
        // See the internet_checksum::update documentation for why we need to
        // provide two bytes which are at an even byte offset from the beginning
        // of the header.
        let old_bytes = [self.hdr_prefix.ttl, self.hdr_prefix.proto];
        let new_bytes = [ttl, self.hdr_prefix.proto];
        self.hdr_prefix.hdr_checksum =
            internet_checksum::update(self.hdr_prefix.hdr_checksum, &old_bytes, &new_bytes);
        self.hdr_prefix.ttl = ttl;
    }
}

impl<B> Debug for Ipv4Packet<B>
where
    B: ByteSlice,
{
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
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

/// A partially parsed and not yet validated IPv4 packet.
///
/// `Ipv4PacketRaw` provides minimal parsing of an IPv4 packet, namely
/// it only requires that the fixed header part ([`HeaderPrefix`]) be retrieved,
/// all the other parts of the packet may be missing when attempting to create
/// it.
///
/// [`Ipv4Packet`] provides a [`FromRaw`] implementation that can be used to
/// validate an `Ipv4PacketRaw`.
pub(crate) struct Ipv4PacketRaw<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    options: MaybeParsed<OptionsRaw<B, Ipv4OptionsImpl>, B>,
    body: MaybeParsed<B, B>,
}

impl<B: ByteSlice> Ipv4Header for Ipv4PacketRaw<B> {
    fn get_header_prefix(&self) -> &HeaderPrefix {
        &self.hdr_prefix
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv4PacketRaw<B> {
    type Error = IpParseError<Ipv4>;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len() + self.options.len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> IpParseResult<Ipv4, Self> {
        let hdr_prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let hdr_bytes = (hdr_prefix.ihl() * 4) as usize;

        let options = MaybeParsed::take_from_buffer_with(
            &mut buffer,
            // If the subtraction hdr_bytes - HDR_PREFIX_LEN would have been
            // negative, that would imply that IHL has an invalid value. Even
            // though this will end up being MaybeParsed::Complete, the IHL
            // value is validated when transforming Ipv4PacketRaw to Ipv4Packet.
            hdr_bytes.saturating_sub(HDR_PREFIX_LEN),
            OptionsRaw::new,
        );

        let total_len: usize = hdr_prefix.total_len.get().into();
        let body_len = total_len.saturating_sub(hdr_bytes);
        if buffer.len() > body_len {
            // Discard the padding left by the previous layer. This unwrap is
            // safe because of the check against total_len.
            buffer.take_back(buffer.len() - body_len).unwrap();
        }

        let body = MaybeParsed::new_with_min_len(buffer.into_rest(), body_len);

        Ok(Self { hdr_prefix, options, body })
    }
}

impl<B: ByteSlice> Ipv4PacketRaw<B> {
    /// Return the body.
    ///
    /// `body` returns [`MaybeParsed::Complete`] if the entire body is present
    /// (as determined by the header's "total length" and "internet header
    /// length" fields), and [`MaybeParsed::Incomplete`] otherwise.
    pub(crate) fn body(&self) -> MaybeParsed<&[u8], &[u8]> {
        self.body.as_ref().map(|b| b.deref()).map_incomplete(|b| b.deref())
    }
}

pub(crate) type Options<B> = crate::wire::records::options::Options<B, Ipv4OptionsImpl>;
pub(crate) type OptionsSerializer<'a, I> =
    crate::wire::records::options::OptionsSerializer<'a, Ipv4OptionsImpl, Ipv4Option<'a>, I>;

/// A PacketBuilder for Ipv4 Packets but with options.
#[derive(Debug)]
pub(crate) struct Ipv4PacketBuilderWithOptions<'a, I: Clone + Iterator<Item = &'a Ipv4Option<'a>>> {
    prefix_builder: Ipv4PacketBuilder,
    options: OptionsSerializer<'a, I>,
}

impl<'a, I: Clone + Iterator<Item = &'a Ipv4Option<'a>>> Ipv4PacketBuilderWithOptions<'a, I> {
    pub(crate) fn new<T: IntoIterator<Item = &'a Ipv4Option<'a>, IntoIter = I>>(
        prefix_builder: Ipv4PacketBuilder,
        options: T,
    ) -> Option<Ipv4PacketBuilderWithOptions<'a, I>> {
        let options = OptionsSerializer::new(options.into_iter());
        // The maximum header length for IPv4 packet is 60 bytes, minus the fixed 40 bytes,
        // we only have 40 bytes for options.
        if options.records_bytes_len() > MAX_OPTIONS_LEN {
            return None;
        }
        Some(Ipv4PacketBuilderWithOptions { prefix_builder, options })
    }

    fn aligned_options_len(&self) -> usize {
        let raw_len = self.options.records_bytes_len();
        // align to the next 4-byte boundary.
        next_multiple_of_four(raw_len)
    }
}

fn next_multiple_of_four(x: usize) -> usize {
    (x + 3) & !3
}

impl<'a, I: Clone + Iterator<Item = &'a Ipv4Option<'a>>> PacketBuilder
    for Ipv4PacketBuilderWithOptions<'a, I>
{
    fn constraints(&self) -> PacketConstraints {
        let header_len = IPV4_MIN_HDR_LEN + self.aligned_options_len();
        assert_eq!(header_len % 4, 0);
        PacketConstraints::new(header_len, 0, 0, (1 << 16) - 1 - header_len)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let mut hdr_prefix =
            header.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for IPv4 header");
        let opt_len = self.aligned_options_len();
        let options = header.take_back_zero(opt_len).expect("too few bytes for Ipv4 options");
        self.options.serialize_records(options);
        self.prefix_builder.assemble(&mut hdr_prefix, options, body.len());
    }
}

/// A builder for IPv4 packets.
#[derive(Debug, Clone, Eq, PartialEq)]
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
    pub(crate) fn new<S: Into<Ipv4Addr>, D: Into<Ipv4Addr>>(
        src_ip: S,
        dst_ip: D,
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
            src_ip: src_ip.into(),
            dst_ip: dst_ip.into(),
        }
    }

    /// Set the Differentiated Services Code Point (DSCP).
    ///
    /// # Panics
    ///
    /// `dscp` panics if `dscp` is greater than 2^6 - 1.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn dscp(&mut self, dscp: u8) {
        assert!(dscp <= 1 << 6, "invalid DCSP: {}", dscp);
        self.dscp = dscp;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 3, "invalid ECN: {}", ecn);
        self.ecn = ecn;
    }

    /// Set the identification.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn id(&mut self, id: u16) {
        self.id = id;
    }

    /// Set the Don't Fragment (DF) flag.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn df_flag(&mut self, df: bool) {
        if df {
            self.flags |= 1 << DF_FLAG_OFFSET;
        } else {
            self.flags &= !(1 << DF_FLAG_OFFSET);
        }
    }

    /// Set the More Fragments (MF) flag.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
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
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn fragment_offset(&mut self, fragment_offset: u16) {
        assert!(fragment_offset < 1 << 13, "invalid fragment offset: {}", fragment_offset);
        self.frag_off = fragment_offset;
    }
}

impl Ipv4PacketBuilder {
    /// Fill in the `HeaderPrefix` part according to the given `options` and `body`.
    fn assemble<B: ByteSliceMut>(
        &self,
        hdr_prefix: &mut LayoutVerified<B, HeaderPrefix>,
        options: &[u8],
        body_len: usize,
    ) {
        let header_len = hdr_prefix.bytes().len() + options.len();
        let total_len = header_len + body_len;
        assert_eq!(header_len % 4, 0);
        let ihl: u8 = u8::try_from(header_len / 4).expect("Header too large");
        hdr_prefix.version_ihl = (4u8 << 4) | ihl;
        hdr_prefix.dscp_ecn = (self.dscp << 2) | self.ecn;
        // The caller promises to supply a body whose length does not exceed
        // max_body_len. Doing this as a debug_assert (rather than an assert) is
        // fine because, with debug assertions disabled, we'll just write an
        // incorrect header value, which is acceptable if the caller has
        // violated their contract.
        debug_assert!(total_len <= std::u16::MAX as usize);
        hdr_prefix.total_len = U16::new(total_len as u16);
        hdr_prefix.id = U16::new(self.id);
        NetworkEndian::write_u16(
            &mut hdr_prefix.flags_frag_off,
            ((u16::from(self.flags)) << 13) | self.frag_off,
        );
        hdr_prefix.ttl = self.ttl;
        hdr_prefix.proto = self.proto;
        hdr_prefix.src_ip = self.src_ip;
        hdr_prefix.dst_ip = self.dst_ip;
        let checksum = compute_header_checksum(hdr_prefix.bytes(), options);
        hdr_prefix.hdr_checksum = checksum;
    }
}

impl PacketBuilder for Ipv4PacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(IPV4_MIN_HDR_LEN, 0, 0, (1 << 16) - 1 - IPV4_MIN_HDR_LEN)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // SECURITY: Use _zero constructor to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let mut hdr_prefix =
            header.take_obj_front_zero::<HeaderPrefix>().expect("too few bytes for IPv4 header");
        self.assemble(&mut hdr_prefix, &[][..], body.len());
    }
}

// bit positions into the flags bits
const DF_FLAG_OFFSET: u32 = 1;
const MF_FLAG_OFFSET: u32 = 0;

pub(crate) mod options {
    use byteorder::{ByteOrder, NetworkEndian, WriteBytesExt};

    use crate::ip::{Ipv4Option, Ipv4OptionData};
    use crate::wire::records::options::{OptionsImpl, OptionsImplLayout, OptionsSerializerImpl};

    const OPTION_KIND_EOL: u8 = 0;
    const OPTION_KIND_NOP: u8 = 1;
    const OPTION_KIND_RTRALRT: u8 = 148;

    const OPTION_RTRALRT_LEN: usize = 2;

    #[derive(Debug)]
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
                    unreachable!("wire::records::options::Options promises to handle EOL and NOP")
                }
                self::OPTION_KIND_RTRALRT => {
                    if data.len() == OPTION_RTRALRT_LEN {
                        Ok(Some(Ipv4Option {
                            copied,
                            data: Ipv4OptionData::RouterAlert {
                                data: NetworkEndian::read_u16(data),
                            },
                        }))
                    } else {
                        Err(())
                    }
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

    impl<'a> OptionsSerializerImpl<'a> for Ipv4OptionsImpl {
        type Option = Ipv4Option<'a>;

        fn get_option_length(option: &Self::Option) -> usize {
            match option.data {
                Ipv4OptionData::RouterAlert { .. } => OPTION_RTRALRT_LEN,
                Ipv4OptionData::Unrecognized { len, .. } => len as usize,
            }
        }

        fn get_option_kind(option: &Self::Option) -> u8 {
            let number = match option.data {
                Ipv4OptionData::RouterAlert { .. } => OPTION_KIND_RTRALRT,
                Ipv4OptionData::Unrecognized { kind, .. } => kind,
            };
            number | ((option.copied as u8) << 7)
        }

        fn serialize(mut buffer: &mut [u8], option: &Self::Option) {
            match option.data {
                Ipv4OptionData::Unrecognized { data, .. } => buffer.copy_from_slice(data),
                Ipv4OptionData::RouterAlert { data } => {
                    buffer.write_u16::<NetworkEndian>(data).unwrap()
                }
            };
        }
    }

    #[cfg(test)]
    mod test {
        use super::*;
        use crate::wire::records::options::Options;
        use crate::wire::records::RecordsSerializerImpl;

        #[test]
        fn test_serialize_router_alert() {
            let mut buffer = [0u8; 4];
            let option = Ipv4Option { copied: true, data: Ipv4OptionData::RouterAlert { data: 0 } };
            <Ipv4OptionsImpl as RecordsSerializerImpl>::serialize(&mut buffer, &option);
            assert_eq!(buffer[0], 148);
            assert_eq!(buffer[1], 4);
            assert_eq!(buffer[2], 0);
            assert_eq!(buffer[3], 0);
        }

        #[test]
        fn test_parse_router_alert() {
            let mut buffer: Vec<u8> = vec![148, 4, 0, 0];
            let options = Options::<_, Ipv4OptionsImpl>::parse(buffer.as_mut()).unwrap();
            let rtralt = options.iter().nth(0).unwrap();
            assert!(rtralt.copied);
            assert_eq!(rtralt.data, Ipv4OptionData::RouterAlert { data: 0 });
        }
    }
}

#[cfg(test)]
mod tests {
    use net_types::ethernet::Mac;
    use net_types::ip::Ipv4;
    use packet::{Buf, FragmentedBuffer, InnerPacketBuilder, ParseBuffer, Serializer};

    use super::*;
    use crate::device::ethernet::EtherType;
    use crate::ip::IpExt;
    use crate::testutil::*;
    use crate::wire::ethernet::{EthernetFrame, EthernetFrameBuilder};

    const DEFAULT_SRC_MAC: Mac = Mac::new([1, 2, 3, 4, 5, 6]);
    const DEFAULT_DST_MAC: Mac = Mac::new([7, 8, 9, 0, 1, 2]);
    const DEFAULT_SRC_IP: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const DEFAULT_DST_IP: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);

    #[test]
    fn test_parse_serialize_full_tcp() {
        use crate::wire::testdata::tls_client_hello_v4::*;

        let mut buf = &ETHERNET_FRAME.bytes[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        verify_ipv4_packet(&packet, IPV4_PACKET);

        let buffer = packet
            .body()
            .into_serializer()
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);
    }

    #[test]
    fn test_parse_serialize_full_udp() {
        use crate::wire::testdata::dns_request_v4::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv4Packet<_>>().unwrap();
        verify_ipv4_packet(&packet, IPV4_PACKET);

        let buffer = packet
            .body()
            .into_serializer()
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);
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
        hdr_prefix.total_len = U16::new(20);
        hdr_prefix.id = U16::new(0x0102);
        hdr_prefix.ttl = 0x03;
        hdr_prefix.proto = IpProto::Tcp.into();
        hdr_prefix.src_ip = DEFAULT_SRC_IP;
        hdr_prefix.dst_ip = DEFAULT_DST_IP;
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

    #[test]
    fn test_parse_padding() {
        // Test that we properly discard post-packet padding.
        let mut buffer = Buf::new(vec![], ..)
            .encapsulate(<Ipv4 as IpExt>::PacketBuilder::new(
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
            .serialize_vec_outer()
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
            ParseError::Format.into()
        );

        // Set the IHL to 4, implying a header length of 16. This is smaller
        // than the minimum of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 4;
        assert_eq!(
            (&hdr_prefix_to_bytes(hdr_prefix)[..]).parse::<Ipv4Packet<_>>().unwrap_err(),
            ParseError::Format.into()
        );

        // Set the IHL to 6, implying a header length of 24. This is larger than
        // the actual packet length of 20.
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 6;
        assert_eq!(
            (&hdr_prefix_to_bytes(hdr_prefix)[..]).parse::<Ipv4Packet<_>>().unwrap_err(),
            ParseError::Format.into()
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

        let mut buf = (&[0, 1, 2, 3, 3, 4, 5, 7, 8, 9])
            .into_serializer()
            .encapsulate(builder)
            .serialize_vec_outer()
            .unwrap();
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
        Buf::new(&mut buf_0[..], IPV4_MIN_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
        let mut buf_1 = [0xFF; IPV4_MIN_HDR_LEN];
        Buf::new(&mut buf_1[..], IPV4_MIN_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic(expected = "(Mtu, Nested { inner: Buf { buf:")]
    fn test_serialize_panic_packet_length() {
        // Test that a packet which is longer than 2^16 - 1 bytes is rejected.
        Buf::new(&mut [0; (1 << 16) - IPV4_MIN_HDR_LEN][..], ..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
    }

    #[test]
    fn test_copy_header_bytes_for_fragment() {
        let hdr_prefix = new_hdr_prefix();
        let mut bytes = hdr_prefix_to_bytes(hdr_prefix);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv4Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        bytes[IPV4_FRAGMENT_DATA_BYTE_RANGE].copy_from_slice(&[0; 4][..]);
        assert_eq!(&copied_bytes[..], &bytes[..]);
    }

    #[test]
    fn test_partial_parsing() {
        // Try something with only the header, but that would have a larger
        // body:
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.total_len = U16::new(256);
        let mut bytes = hdr_prefix_to_bytes(hdr_prefix)[..].to_owned();
        bytes.extend(&[1, 2, 3, 4, 5]);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv4PacketRaw<_>>().unwrap();
        assert_eq!(packet.hdr_prefix.bytes(), &bytes[0..20]);
        assert_eq!(packet.options.as_ref().unwrap().len(), 0);
        // We must've captured the incomplete bytes in body:
        assert_eq!(packet.body.as_ref().unwrap_incomplete().len(), 5);
        // validation should fail:
        assert!(Ipv4Packet::try_from_raw(packet).is_err());

        // Try something with the header plus incomplete options:
        let mut hdr_prefix = new_hdr_prefix();
        hdr_prefix.version_ihl = (4 << 4) | 10;
        let bytes = hdr_prefix_to_bytes(hdr_prefix);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv4PacketRaw<_>>().unwrap();
        assert_eq!(packet.hdr_prefix.bytes(), bytes);
        assert!(packet.options.is_incomplete());
        assert!(packet.body.is_complete());
        // validation should fail:
        assert!(Ipv4Packet::try_from_raw(packet).is_err());

        // Try an incomplete header:
        let hdr_prefix = new_hdr_prefix();
        let bytes = &hdr_prefix_to_bytes(hdr_prefix);
        let mut buf = &bytes[0..10];
        assert!(buf.parse::<Ipv4PacketRaw<_>>().is_err());
    }

    #[test]
    fn test_next_multiple_of_four() {
        for x in 0usize..=(std::u16::MAX - 3) as usize {
            let y = next_multiple_of_four(x);
            assert_eq!(y % 4, 0);
            assert!(y >= x);
            if x % 4 == 0 {
                assert_eq!(x, y);
            } else {
                assert_eq!(x + (4 - x % 4), y);
            }
        }
    }
}
