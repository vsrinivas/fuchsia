// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 packets.
//!
//! The IPv6 packet format is defined in [RFC 8200] Sections 3 and 4.
//!
//! [RFC 8200]: https://datatracker.ietf.org/doc/html/rfc8200

pub mod ext_hdrs;

use alloc::vec::Vec;
use core::borrow::Borrow;
use core::convert::TryFrom;
use core::fmt::{self, Debug, Formatter};
use core::ops::Range;

use log::debug;
use net_types::ip::{Ipv4Addr, Ipv6, Ipv6Addr, Ipv6SourceAddr};
use packet::records::{AlignedRecordSequenceBuilder, Records, RecordsRaw};
use packet::{
    BufferProvider, BufferView, BufferViewMut, EmptyBuf, FromRaw, InnerPacketBuilder, MaybeParsed,
    NestedPacketBuilder, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
    SerializeBuffer, SerializeError, Serializer, TargetBuffer,
};
use zerocopy::{
    byteorder::network_endian::U16, AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified,
    Unaligned,
};

use crate::error::{IpParseError, IpParseErrorAction, IpParseResult, ParseError};
use crate::icmp::Icmpv6ParameterProblemCode;
use crate::ip::{
    IpPacketBuilder, IpProto, Ipv4Proto, Ipv6ExtHdrType, Ipv6Proto, Nat64Error,
    Nat64TranslationResult,
};
use crate::ipv4::{Ipv4PacketBuilder, HDR_PREFIX_LEN};
use crate::ipv6::ext_hdrs::{HopByHopOption, HopByHopOptionData};
use crate::tcp::{TcpParseArgs, TcpSegment};
use crate::udp::{UdpPacket, UdpParseArgs};

use ext_hdrs::{
    is_valid_next_header, is_valid_next_header_upper_layer, ExtensionHeaderOptionAction,
    Ipv6ExtensionHeader, Ipv6ExtensionHeaderData, Ipv6ExtensionHeaderImpl,
    Ipv6ExtensionHeaderParsingContext, Ipv6ExtensionHeaderParsingError, IPV6_FRAGMENT_EXT_HDR_LEN,
};

/// Length of the IPv6 fixed header.
pub const IPV6_FIXED_HDR_LEN: usize = 40;

/// The range of bytes within an IPv6 header buffer that the
/// payload length field uses.
pub const IPV6_PAYLOAD_LEN_BYTE_RANGE: Range<usize> = 4..6;

// Offset to the Next Header field within the fixed IPv6 header
const NEXT_HEADER_OFFSET: u8 = 6;

// The maximum length for Hop-by-Hop Options. The stored byte's maximum
// representable value is `core::u8::MAX` and it means the header has
// that many 8-octets, not including the first 8 octets.
const IPV6_HBH_OPTIONS_MAX_LEN: usize = (core::u8::MAX as usize) * 8 + 8;

/// Convert an extension header parsing error to an IP packet
/// parsing error.
fn ext_hdr_err_fn(hdr: &FixedHeader, err: Ipv6ExtensionHeaderParsingError) -> IpParseError<Ipv6> {
    // Below, we set parameter problem data's `pointer` to `IPV6_FIXED_HDR_LEN` + `pointer`
    // since the the `pointer` we get from an `Ipv6ExtensionHeaderParsingError` is calculated
    // from the start of the extension headers. Within an IPv6 packet, extension headers
    // start right after the fixed header with a length of `IPV6_FIXED_HDR_LEN` so we add `pointer`
    // to `IPV6_FIXED_HDR_LEN` to get the pointer to the field with the parameter problem error
    // from the start of the IPv6 packet. For a non-jumbogram packet, we know that
    // `IPV6_FIXED_HDR_LEN` + `pointer` will not overflow because the maximum size of an
    // IPv6 packet is 65575 bytes (fixed header + extension headers + body) and 65575 definitely
    // fits within an `u32`. This may no longer hold true if/when jumbogram packets are supported.
    // For the jumbogram case when the size of extension headers could be >= (4 GB - 41 bytes) (which
    // we almost certainly will never encounter), the pointer calculation may overflow. To account for
    // this scenario, we check for overflows when adding `IPV6_FIXED_HDR_LEN` to `pointer`. If
    // we do end up overflowing, we will discard the packet (even if we were normally required to
    // send back an ICMP error message) because we will be unable to construct a correct ICMP error
    // message (the pointer field of the ICMP message will not be able to hold a value > (4^32 - 1)
    // which is what we would have if the pointer calculation overflows). But again, we should almost
    // never encounter this scenario so we don't care if we have incorrect behaviour.

    match err {
        Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
            pointer,
            must_send_icmp,
            header_len: _,
        } => {
            let (pointer, action) = match pointer.checked_add(IPV6_FIXED_HDR_LEN as u32) {
                // Pointer calculation overflowed so set action to discard the packet and
                // 0 for the pointer (which won't be used anyways since the packet will be
                // discarded without sending an ICMP response).
                None => (0, IpParseErrorAction::DiscardPacket),
                // Pointer calculation didn't overflow so set action to send an ICMP
                // message to the source of the original packet and the pointer value
                // to what we calculated.
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendIcmpNoMulticast),
            };

            IpParseError::ParameterProblem {
                src_ip: hdr.src_ip,
                dst_ip: hdr.dst_ip,
                code: Icmpv6ParameterProblemCode::ErroneousHeaderField,
                pointer,
                must_send_icmp,
                header_len: (),
                action,
            }
        }
        Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len: _,
        } => {
            let (pointer, action) = match pointer.checked_add(IPV6_FIXED_HDR_LEN as u32) {
                None => (0, IpParseErrorAction::DiscardPacket),
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendIcmpNoMulticast),
            };

            IpParseError::ParameterProblem {
                src_ip: hdr.src_ip,
                dst_ip: hdr.dst_ip,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer,
                must_send_icmp,
                header_len: (),
                action,
            }
        }
        Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len: _,
            action,
        } => {
            let (pointer, action) = match pointer.checked_add(IPV6_FIXED_HDR_LEN as u32) {
                None => (0, IpParseErrorAction::DiscardPacket),
                Some(p) => {
                    let action = match action {
                        ExtensionHeaderOptionAction::SkipAndContinue => unreachable!(
                            "Should never end up here because this action should never result in an error"
                        ),
                        ExtensionHeaderOptionAction::DiscardPacket => IpParseErrorAction::DiscardPacket,
                        ExtensionHeaderOptionAction::DiscardPacketSendIcmp => {
                            IpParseErrorAction::DiscardPacketSendIcmp
                        }
                        ExtensionHeaderOptionAction::DiscardPacketSendIcmpNoMulticast => {
                            IpParseErrorAction::DiscardPacketSendIcmpNoMulticast
                        }
                    };

                    (p, action)
                }
            };

            IpParseError::ParameterProblem {
                src_ip: hdr.src_ip,
                dst_ip: hdr.dst_ip,
                code: Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
                pointer,
                must_send_icmp,
                header_len: (),
                action,
            }
        }
        Ipv6ExtensionHeaderParsingError::BufferExhausted
        | Ipv6ExtensionHeaderParsingError::MalformedData => {
            // Unexpectedly running out of a buffer or encountering malformed
            // data when parsing is a formatting error.
            IpParseError::Parse { error: ParseError::Format }
        }
    }
}

/// The IPv6 fixed header which precedes any extension headers and the body.
#[derive(Debug, Default, FromBytes, AsBytes, Unaligned, PartialEq)]
#[repr(C)]
pub struct FixedHeader {
    version_tc_flowlabel: [u8; 4],
    payload_len: U16,
    next_hdr: u8,
    hop_limit: u8,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
}

const IP_VERSION: u8 = 6;
const VERSION_OFFSET: u8 = 4;
const DS_OFFSET: u8 = 2;
const DS_MAX: u8 = (1 << (8 - DS_OFFSET)) - 1;
const ECN_MAX: u8 = (1 << DS_OFFSET) - 1;
const FLOW_LABEL_MAX: u32 = (1 << 20) - 1;

impl FixedHeader {
    #[allow(clippy::too_many_arguments)]
    fn new(
        ds: u8,
        ecn: u8,
        flow_label: u32,
        payload_len: u16,
        next_hdr: u8,
        hop_limit: u8,
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
    ) -> FixedHeader {
        debug_assert!(ds <= DS_MAX);
        debug_assert!(ecn <= ECN_MAX);
        debug_assert!(flow_label <= FLOW_LABEL_MAX);

        let traffic_class = (ds << DS_OFFSET) | ecn;
        FixedHeader {
            version_tc_flowlabel: [
                IP_VERSION << VERSION_OFFSET | traffic_class >> 4,
                (traffic_class << 4) | ((flow_label >> 16) as u8),
                (flow_label >> 8) as u8,
                flow_label as u8,
            ],
            payload_len: U16::new(payload_len),
            next_hdr,
            hop_limit,
            src_ip,
            dst_ip,
        }
    }

    fn version(&self) -> u8 {
        self.version_tc_flowlabel[0] >> 4
    }

    fn ds(&self) -> u8 {
        (self.version_tc_flowlabel[0] & 0xF) << 2 | self.version_tc_flowlabel[1] >> 6
    }

    fn ecn(&self) -> u8 {
        (self.version_tc_flowlabel[1] & 0x30) >> 4
    }

    fn flowlabel(&self) -> u32 {
        (u32::from(self.version_tc_flowlabel[1]) & 0xF) << 16
            | u32::from(self.version_tc_flowlabel[2]) << 8
            | u32::from(self.version_tc_flowlabel[3])
    }
}

/// Provides common access to IPv6 header fields.
///
/// `Ipv6Header` provides access to IPv6 header fields as a common
/// implementation for both [`Ipv6Packet`] and [`Ipv6PacketRaw`].
pub trait Ipv6Header {
    /// Gets a reference to the IPv6 [`FixedHeader`].
    fn get_fixed_header(&self) -> &FixedHeader;

    /// The Hop Limit.
    fn hop_limit(&self) -> u8 {
        self.get_fixed_header().hop_limit
    }

    /// The Next Header.
    fn next_header(&self) -> u8 {
        self.get_fixed_header().next_hdr
    }

    /// The source IP address.
    fn src_ip(&self) -> Ipv6Addr {
        self.get_fixed_header().src_ip
    }

    /// The destination IP address.
    fn dst_ip(&self) -> Ipv6Addr {
        self.get_fixed_header().dst_ip
    }
}

/// An IPv6 packet.
///
/// An `Ipv6Packet` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub struct Ipv6Packet<B> {
    fixed_hdr: LayoutVerified<B, FixedHeader>,
    extension_hdrs: Records<B, Ipv6ExtensionHeaderImpl>,
    body: B,
    proto: Ipv6Proto,
}

impl<B: ByteSlice> Ipv6Header for Ipv6Packet<B> {
    fn get_fixed_header(&self) -> &FixedHeader {
        &self.fixed_hdr
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv6Packet<B> {
    type Error = IpParseError<Ipv6>;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.fixed_hdr.bytes().len() + self.extension_hdrs.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, _args: ()) -> IpParseResult<Ipv6, Self> {
        Ipv6PacketRaw::parse(buffer, ()).and_then(Ipv6Packet::try_from_raw)
    }
}

impl<B: ByteSlice> FromRaw<Ipv6PacketRaw<B>, ()> for Ipv6Packet<B> {
    type Error = IpParseError<Ipv6>;

    fn try_from_raw_with(raw: Ipv6PacketRaw<B>, _args: ()) -> Result<Self, Self::Error> {
        let fixed_hdr = raw.fixed_hdr;

        // Make sure that the fixed header has a valid next header before
        // validating extension headers.
        if !is_valid_next_header(fixed_hdr.next_hdr, true) {
            return debug_err!(
                Err(IpParseError::ParameterProblem {
                    src_ip: fixed_hdr.src_ip,
                    dst_ip: fixed_hdr.dst_ip,
                    code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                    pointer: u32::from(NEXT_HEADER_OFFSET),
                    must_send_icmp: false,
                    header_len: (),
                    action: IpParseErrorAction::DiscardPacketSendIcmpNoMulticast,
                }),
                "Unrecognized next header value"
            );
        }

        let extension_hdrs = match raw.extension_hdrs {
            MaybeParsed::Complete(v) => {
                Records::try_from_raw(v).map_err(|e| ext_hdr_err_fn(&fixed_hdr, e))?
            }
            MaybeParsed::Incomplete(_) => {
                return debug_err!(
                    Err(ParseError::Format.into()),
                    "Incomplete IPv6 extension headers"
                );
            }
        };

        // If extension headers parse successfully, then proto and a
        // `MaybeParsed` body MUST be available, and the proto must be a valid
        // next header for upper layers.
        let (body, proto) =
            raw.body_proto.expect("Unable to retrieve Ipv6Proto or MaybeParsed body from raw");
        debug_assert!(is_valid_next_header_upper_layer(proto.into()));

        let body = match body {
            MaybeParsed::Complete(b) => b,
            MaybeParsed::Incomplete(_b) => {
                return debug_err!(Err(ParseError::Format.into()), "IPv6 body unretrievable.");
            }
        };

        // check that the lengths match:
        //
        // As per Section 3 of RFC 8200, payload length includes the length of
        // the extension headers as well.
        if extension_hdrs.bytes().len() + body.len() != usize::from(fixed_hdr.payload_len.get()) {
            return debug_err!(
                Err(ParseError::Format.into()),
                "Payload len does not match body and extension headers"
            );
        }

        // validate IP version in header
        if fixed_hdr.version() != 6 {
            return debug_err!(
                Err(ParseError::Format.into()),
                "unexpected IP version: {}",
                fixed_hdr.version()
            );
        }

        Ok(Ipv6Packet { fixed_hdr, extension_hdrs, body, proto })
    }
}

impl<B: ByteSlice> Ipv6Packet<B> {
    /// Returns an iterator over the extension headers.
    pub fn iter_extension_hdrs(&self) -> impl Iterator<Item = Ipv6ExtensionHeader<'_>> {
        self.extension_hdrs.iter()
    }

    /// The packet body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// The Differentiated Services (DS) field.
    pub fn ds(&self) -> u8 {
        self.fixed_hdr.ds()
    }

    /// The Explicit Congestion Notification (ECN).
    pub fn ecn(&self) -> u8 {
        self.fixed_hdr.ecn()
    }

    /// The flow label.
    pub fn flowlabel(&self) -> u32 {
        self.fixed_hdr.flowlabel()
    }

    /// The Upper layer protocol for this packet.
    ///
    /// This is found in the fixed header's Next Header if there are no extension
    /// headers, or the Next Header value in the last extension header if there are.
    /// This also  uses the same codes, encoded by the Rust type `Ipv6Proto`.
    pub fn proto(&self) -> Ipv6Proto {
        self.proto
    }

    /// The source IP address represented as an [`Ipv6SourceAddr`].
    ///
    /// Unlike [`IpHeader::src_ip`], `src_ipv6` returns an `Ipv6SourceAddr`,
    /// which represents the valid values that a source address can take
    /// (namely, a unicast or unspecified address) or `None` if the address is
    /// invalid (namely, a multicast address).
    pub fn src_ipv6(&self) -> Option<Ipv6SourceAddr> {
        Ipv6SourceAddr::new(self.fixed_hdr.src_ip)
    }

    /// Return a buffer that is a copy of the header bytes in this
    /// packet, including the fixed and extension headers, but without
    /// the first fragment extension header.
    ///
    /// Note, if there are multiple fragment extension headers, only
    /// the first fragment extension header will be removed.
    ///
    /// # Panics
    ///
    /// Panics if there is no fragment extension header in this packet.
    pub fn copy_header_bytes_for_fragment(&self) -> Vec<u8> {
        // Since the final header will not include a fragment header, we don't
        // need to allocate bytes for it (`IPV6_FRAGMENT_EXT_HDR_LEN` bytes).
        let expected_bytes_len = self.header_len() - IPV6_FRAGMENT_EXT_HDR_LEN;
        let mut bytes = Vec::with_capacity(expected_bytes_len);

        bytes.extend_from_slice(self.fixed_hdr.bytes());

        // We cannot simply copy over the extension headers because we want
        // discard the first fragment header, so we iterate over our
        // extension headers and find out where our fragment header starts at.
        let mut iter = self.extension_hdrs.iter();

        // This should never panic because we must only call this function
        // when the packet is fragmented so it must have at least one extension
        // header (the fragment extension header).
        let ext_hdr = iter.next().expect("packet must have at least one extension header");

        if self.fixed_hdr.next_hdr == Ipv6ExtHdrType::Fragment.into() {
            // The fragment header is the first extension header so
            // we need to patch the fixed header.

            // Update the next header value in the fixed header within the buffer
            // to the next header value from the fragment header.
            bytes[6] = ext_hdr.next_header;

            // Copy extension headers that appear after the fragment header
            bytes.extend_from_slice(&self.extension_hdrs.bytes()[IPV6_FRAGMENT_EXT_HDR_LEN..]);
        } else {
            let mut ext_hdr = ext_hdr;
            let mut ext_hdr_start = 0;
            let mut ext_hdr_end = iter.context().bytes_parsed;

            // Here we keep looping until `next_ext_hdr` points to the fragment header.
            // Once we find the fragment header, we update the next header value within
            // the extension header preceeding the fragment header, `ext_hdr`. Note,
            // we keep track of where in the extension header buffer the current `ext_hdr`
            // starts and ends so we can patch its next header value.
            loop {
                // This should never panic because if we panic, it means that we got a
                // `None` value from `iter.next()` which would mean we exhausted all the
                // extension headers while looking for the fragment header, meaning there
                // is no fragment header. This function should never be called if there
                // is no fragment extension header in the packet.
                let next_ext_hdr = iter
                    .next()
                    .expect("exhausted all extension headers without finding fragment header");

                if let Ipv6ExtensionHeaderData::Fragment { .. } = next_ext_hdr.data() {
                    // The next extension header is the fragment header
                    // so we copy the buffer before and after the extension header
                    // into `bytes` and patch the next header value within the
                    // current extension header in `bytes`.

                    // Size of the fragment header should be exactly `IPV6_FRAGMENT_EXT_HDR_LEN`.
                    let fragment_hdr_end = ext_hdr_end + IPV6_FRAGMENT_EXT_HDR_LEN;
                    assert_eq!(fragment_hdr_end, iter.context().bytes_parsed);

                    let extension_hdr_bytes = self.extension_hdrs.bytes();

                    // Copy extension headers that appear before the fragment header
                    bytes.extend_from_slice(&extension_hdr_bytes[..ext_hdr_end]);

                    // Copy extension headers that appear after the fragment header
                    bytes.extend_from_slice(&extension_hdr_bytes[fragment_hdr_end..]);

                    // Update the current `ext_hdr`'s next header value to the next
                    // header value within the fragment extension header.
                    match ext_hdr.data() {
                        // The next header value is located in the first byte of the
                        // extension header.
                        Ipv6ExtensionHeaderData::HopByHopOptions { .. }
                        | Ipv6ExtensionHeaderData::DestinationOptions { .. } => {
                            bytes[IPV6_FIXED_HDR_LEN+ext_hdr_start] = next_ext_hdr.next_header;
                        }
                        Ipv6ExtensionHeaderData::Fragment { .. } => unreachable!("If we had a fragment header before `ext_hdr`, we should have used that instead"),
                    }

                    break;
                }

                ext_hdr = next_ext_hdr;
                ext_hdr_start = ext_hdr_end;
                ext_hdr_end = iter.context().bytes_parsed;
            }
        }

        // `bytes`'s length should be exactly `expected_bytes_len`.
        assert_eq!(bytes.len(), expected_bytes_len);
        bytes
    }

    fn header_len(&self) -> usize {
        self.fixed_hdr.bytes().len() + self.extension_hdrs.bytes().len()
    }

    fn fragment_header_present(&self) -> bool {
        for ext_hdr in self.extension_hdrs.iter() {
            if matches!(ext_hdr.data(), Ipv6ExtensionHeaderData::Fragment { .. }) {
                return true;
            }
        }
        false
    }

    /// Construct a builder with the same contents as this packet.
    pub fn builder(&self) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: self.ds(),
            ecn: self.ecn(),
            flowlabel: self.flowlabel(),
            hop_limit: self.hop_limit(),
            proto: self.proto(),
            src_ip: self.src_ip(),
            dst_ip: self.dst_ip(),
        }
    }

    /// Performs the header translation part of NAT64 as described in [RFC
    /// 7915].
    ///
    /// `nat64_translate` follows the rules described in RFC 7915 to construct
    /// the IPv4 equivalent of this IPv6 packet. If the payload is a TCP segment
    /// or a UDP packet, its checksum will be updated. If the payload is an
    /// ICMPv6 packet, it will be converted to the equivalent ICMPv4 packet. For
    /// all other payloads, the payload will be unchanged, and the IP header will
    /// be translated. On success, a [`Serializer`] is returned which describes
    /// the new packet to be sent.
    ///
    /// Note that the IPv4 TTL/IPv6 Hop Limit field is not modified. It is the
    /// caller's responsibility to decrement and process this field per RFC
    /// 7915.
    ///
    /// In some cases, the packet has no IPv4 equivalent, in which case the
    /// value [`Nat64TranslationResult::Drop`] will be returned, instructing the
    /// caller to silently drop the packet.
    ///
    /// # Errors
    ///
    /// `nat64_translate` will return an error if support has not yet been
    /// implemented for translating a particular IP protocol.
    ///
    /// [RFC 7915]: https://datatracker.ietf.org/doc/html/rfc7915
    pub fn nat64_translate(
        &self,
        v4_src_addr: Ipv4Addr,
        v4_dst_addr: Ipv4Addr,
    ) -> Nat64TranslationResult<impl Serializer<Buffer = EmptyBuf> + Debug + '_, Nat64Error> {
        // A single `Serializer` type so that all possible return values from
        // this function have the same type.
        #[derive(Debug)]
        enum Nat64Serializer<T, U, O> {
            Tcp(T),
            Udp(U),
            Other(O),
        }

        impl<T, U, O> Serializer for Nat64Serializer<T, U, O>
        where
            T: Serializer<Buffer = EmptyBuf>,
            U: Serializer<Buffer = EmptyBuf>,
            O: Serializer<Buffer = EmptyBuf>,
        {
            type Buffer = EmptyBuf;
            fn serialize<B, PB, P>(
                self,
                outer: PB,
                provider: P,
            ) -> Result<B, (SerializeError<P::Error>, Self)>
            where
                B: TargetBuffer,
                PB: NestedPacketBuilder,
                P: BufferProvider<Self::Buffer, B>,
            {
                match self {
                    Nat64Serializer::Tcp(serializer) => serializer
                        .serialize(outer, provider)
                        .map_err(|(err, ser)| (err, Nat64Serializer::Tcp(ser))),
                    Nat64Serializer::Udp(serializer) => serializer
                        .serialize(outer, provider)
                        .map_err(|(err, ser)| (err, Nat64Serializer::Udp(ser))),
                    Nat64Serializer::Other(serializer) => serializer
                        .serialize(outer, provider)
                        .map_err(|(err, ser)| (err, Nat64Serializer::Other(ser))),
                }
            }
        }

        // TODO(https://fxbug.dev/92381): Add support for fragmented packets
        // forwarding.
        if self.fragment_header_present() {
            return Nat64TranslationResult::Err(Nat64Error::NotImplemented);
        }

        let v4_builder = |v4_proto| {
            let mut builder =
                Ipv4PacketBuilder::new(v4_src_addr, v4_dst_addr, self.hop_limit(), v4_proto);
            builder.dscp(self.ds());
            builder.ecn(self.ecn());

            // The IPv4 header length is 20 bytes (so IHL field value is 5), as
            // no header options are present in translated IPv4 packet.
            // As per RFC 7915 Section 5.1:
            //  "Internet Header Length:  5 (no IPv4 options)"
            const IPV4_HEADER_LEN_BYTES: usize = HDR_PREFIX_LEN;

            // As per RFC 7915 Section 5.1,
            //    "Flags:  The More Fragments flag is set to zero.  The Don't Fragment
            //        (DF) flag is set as follows: If the size of the translated IPv4
            //        packet is less than or equal to 1260 bytes, it is set to zero;
            //        otherwise, it is set to one."
            builder.df_flag(self.body().len() + IPV4_HEADER_LEN_BYTES > 1260);

            // TODO(https://fxbug.dev/92381): This needs an update once
            // we don't return early for fragment_header_present case.
            builder.fragment_offset(0);
            builder.mf_flag(false);

            builder
        };

        match self.proto() {
            Ipv6Proto::Proto(IpProto::Tcp) => {
                let v4_pkt_builder = v4_builder(Ipv4Proto::Proto(IpProto::Tcp));
                let args = TcpParseArgs::new(self.src_ip(), self.dst_ip());
                // TODO(https://fxbug.dev/92701): We're doing roughly similar work
                // in valid/invalid parsing case. Remove match statement and
                // update the checksum in place without needing to parse the TCP
                // segment once we have ability to update the checksum.
                match TcpSegment::parse(&mut self.body.as_bytes(), args) {
                    Ok(tcp) => {
                        // Creating a new tcp_serializer for IPv6 packet from
                        // the existing one ensures that checksum is
                        // updated due to changed IP addresses.
                        let tcp_serializer =
                            Nat64Serializer::Tcp(tcp.into_serializer(v4_src_addr, v4_dst_addr));
                        Nat64TranslationResult::Forward(tcp_serializer.encapsulate(v4_pkt_builder))
                    }
                    Err(msg) => {
                        debug!("Parsing of TCP segment failed: {:?}", msg);

                        // This means we can't create a TCP segment builder with
                        // updated checksum. Parsing may fail due to a variety of
                        // reasons, including incorrect checksum in incoming packet.
                        // We should still return a packet with IP payload copied
                        // as is from IPv6 to IPv4. This handling is similar to
                        // the handling of the case with unsupported protocol type
                        // as done in `Ipv6Proto::Other(val)` case below. The similar
                        // reasoning from RFC appiles here as well.
                        let common_serializer =
                            Nat64Serializer::Other(self.body().into_serializer());
                        Nat64TranslationResult::Forward(
                            common_serializer.encapsulate(v4_pkt_builder),
                        )
                    }
                }
            }

            // TODO(https://fxbug.dev/92701): We're doing roughly similar work
            // in valid/invalid parsing case. Remove match statement and
            // update the checksum in place without needing to parse the UDP segment
            // once we have ability to update checksum.
            Ipv6Proto::Proto(IpProto::Udp) => {
                let v4_pkt_builder = v4_builder(Ipv4Proto::Proto(IpProto::Udp));
                let args = UdpParseArgs::new(self.src_ip(), self.dst_ip());
                match UdpPacket::parse(&mut self.body.as_bytes(), args) {
                    Ok(udp) => {
                        // Creating a new udp_serializer for IPv6 packet from
                        // the existing one ensures that checksum is
                        // updated due to changed IP addresses.
                        let udp_serializer =
                            Nat64Serializer::Udp(udp.into_serializer(v4_src_addr, v4_dst_addr));
                        Nat64TranslationResult::Forward(udp_serializer.encapsulate(v4_pkt_builder))
                    }
                    Err(msg) => {
                        debug!("Parsing of UDP packet failed: {:?}", msg);

                        // This means we can't create a UDP packet builder with
                        // updated checksum. Parsing may fail due to a variety of
                        // reasons, including incorrect checksum in incoming packet.
                        // We should still return a packet with IP payload copied
                        // as is from IPv6 to IPv4. This handling is similar to
                        // the handling of the case with unsupported protocol type
                        // as done in `Ipv6Proto::Other(val)` case below. The similar
                        // reasoning from RFC appiles here as well.

                        let common_serializer =
                            Nat64Serializer::Other(self.body().into_serializer());
                        Nat64TranslationResult::Forward(
                            common_serializer.encapsulate(v4_pkt_builder),
                        )
                    }
                }
            }

            // TODO(https://fxbug.dev/92383): Implement ICMP packet translation
            // support here.
            Ipv6Proto::Icmpv6 => Nat64TranslationResult::Err(Nat64Error::NotImplemented),

            // For all other protocols, an IPv4 packet must be forwarded even if
            // the transport layer checksum update is not implemented.
            // As per RFC 7915 Section 5.1,
            //     "Protocol:
            //       ...
            //
            //       For the first 'next header' that does not match one of the cases
            //       above, its Next Header value (which contains the transport
            //       protocol number) is copied to the protocol field in the IPv4
            //       header.  This means that all transport protocols are translated.
            //
            //       Note:  Some translated protocols will fail at the receiver for
            //          various reasons: some are known to fail when translated (e.g.,
            //          IPsec Authentication Header (51)), and others will fail
            //          checksum validation if the address translation is not checksum
            //          neutral [RFC6052] and the translator does not update the
            //          transport protocol's checksum (because the translator doesn't
            //          support recalculating the checksum for that transport protocol;
            //          see Section 5.5)."
            Ipv6Proto::Other(val) => {
                let v4_pkt_builder = v4_builder(Ipv4Proto::Other(val));
                let common_serializer = Nat64Serializer::Other(self.body().into_serializer());
                Nat64TranslationResult::Forward(common_serializer.encapsulate(v4_pkt_builder))
            }

            Ipv6Proto::NoNextHeader => {
                let v4_pkt_builder = v4_builder(Ipv4Proto::Other(Ipv6Proto::NoNextHeader.into()));
                let common_serializer = Nat64Serializer::Other(self.body().into_serializer());
                Nat64TranslationResult::Forward(common_serializer.encapsulate(v4_pkt_builder))
            }
        }
    }
}

impl<B: ByteSliceMut> Ipv6Packet<B> {
    /// Set the hop limit.
    pub fn set_hop_limit(&mut self, hlim: u8) {
        self.fixed_hdr.hop_limit = hlim;
    }
}

impl<B: ByteSlice> Debug for Ipv6Packet<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        f.debug_struct("Ipv6Packet")
            .field("src_ip", &self.src_ip())
            .field("dst_ip", &self.dst_ip())
            .field("hop_limit", &self.hop_limit())
            .field("proto", &self.proto())
            .field("ds", &self.ds())
            .field("ecn", &self.ecn())
            .field("flowlabel", &self.flowlabel())
            .field("extension headers", &"TODO")
            .field("body", &alloc::format!("<{} bytes>", self.body.len()))
            .finish()
    }
}

/// We were unable to parse the extension headers.
///
/// As a result, we were unable to determine the upper-layer Protocol Number
/// (which is stored in the last extension header's Next Header field) and were
/// unable figure out where the body begins.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct ExtHdrParseError;

/// A partially parsed and not yet validated IPv6 packet.
///
/// `Ipv6PacketRaw` provides minimal parsing of an IPv6 packet, namely
/// it only requires that the fixed header part ([`HeaderPrefix`]) be retrieved,
/// all the other parts of the packet may be missing when attempting to create
/// it.
///
/// [`Ipv6Packet`] provides a [`FromRaw`] implementation that can be used to
/// validate an `Ipv6PacketRaw`.
pub struct Ipv6PacketRaw<B> {
    /// A raw packet always contains at least a fully parsed `FixedHeader`.
    fixed_hdr: LayoutVerified<B, FixedHeader>,
    /// When `extension_hdrs` is [`MaybeParsed::Complete`], it contains the
    /// `RecordsRaw` that can be validated for full extension headers parsing.
    /// Otherwise, it just contains the extension header bytes that were
    /// successfully consumed before reaching an error (typically "buffer
    /// exhausted").
    extension_hdrs: MaybeParsed<RecordsRaw<B, Ipv6ExtensionHeaderImpl>, B>,
    /// The body and upper-layer Protocol Number.
    ///
    /// If extension headers failed to parse, this will be
    /// `Err(ExtHdrParseError)`. Extension headers must be parsed in order to
    /// find the bounds of the upper-layer payload and to find that last
    /// extension header's Next Header field, which is the Protocol Number of
    /// the upper-layer payload.
    ///
    /// The body will be [`MaybeParsed::Complete`] if all the body bytes were
    /// consumed (as stated by the header's payload length value) or
    /// [`MaybeParsed::Incomplete`] containing the bytes that were present
    /// otherwise.
    body_proto: Result<(MaybeParsed<B, B>, Ipv6Proto), ExtHdrParseError>,
}

impl<B: ByteSlice> Ipv6Header for Ipv6PacketRaw<B> {
    fn get_fixed_header(&self) -> &FixedHeader {
        &self.fixed_hdr
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv6PacketRaw<B> {
    type Error = IpParseError<Ipv6>;

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let fixed_hdr = buffer
            .take_obj_front::<FixedHeader>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let payload_len = fixed_hdr.payload_len.get().into();
        // Trim the buffer if it exceeds the length specified in the header.
        let _: Option<B> = buffer.len().checked_sub(payload_len).map(|padding| {
            buffer.take_back(padding).unwrap_or_else(|| {
                panic!("buffer.len()={} padding={}", buffer.len(), padding);
            })
        });

        let mut extension_hdr_context = Ipv6ExtensionHeaderParsingContext::new(fixed_hdr.next_hdr);

        let extension_hdrs =
            RecordsRaw::parse_raw_with_mut_context(&mut buffer, &mut extension_hdr_context)
                .map_incomplete(|(b, _)| b);

        let body_proto = match &extension_hdrs {
            MaybeParsed::Complete(r) => {
                let _: &RecordsRaw<B, _> = r;
                // If we have extension headers our context's
                // (`extension_hdr_context`) `next_header` would be updated with
                // the last extension header's Next Header value. This will also
                // work if we don't have any extension headers. Let's consider
                // that scenario: When we have no extension headers, the Next
                // Header value in the fixed header will be a valid upper layer
                // protocol value.  `parse_bv_with_mut_context` will return
                // almost immediately without doing any actual work when it
                // checks the context's (`extension_hdr_context`) `next_header`
                // value and ends parsing since, according to our context, its
                // data is for an upper layer protocol. Now, since nothing was
                // parsed, our context was never modified, so the next header
                // value it was initialized with when calling
                // `Ipv6ExtensionHeaderParsingContext::new`, will not have
                // changed. We simply use that value and assign it to proto
                // below.

                // Extension header raw parsing only finishes when we have a
                // valid next header that is meant for the upper layer. The
                // assertion below enforces that contract.
                assert!(is_valid_next_header_upper_layer(extension_hdr_context.next_header));
                let proto = Ipv6Proto::from(extension_hdr_context.next_header);
                let body = MaybeParsed::new_with_min_len(
                    buffer.into_rest(),
                    payload_len.saturating_sub(extension_hdrs.len()),
                );
                Ok((body, proto))
            }
            MaybeParsed::Incomplete(b) => {
                let _: &B = b;
                Err(ExtHdrParseError)
            }
        };

        Ok(Ipv6PacketRaw { fixed_hdr, extension_hdrs, body_proto })
    }

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.fixed_hdr.bytes().len() + self.extension_hdrs.len();
        let body_len = self.body_proto.as_ref().map(|(b, _p)| b.len()).unwrap_or(0);
        ParseMetadata::from_packet(header_len, body_len, 0)
    }
}

impl<B: ByteSlice> Ipv6PacketRaw<B> {
    /// Returns the body and upper-layer Protocol Number.
    ///
    /// If extension headers failed to parse, `body_proto` returns
    /// `Err(ExtHdrParseError)`. Extension headers must be parsed in order to
    /// find the bounds of the upper-layer payload and to find that last
    /// extension header's Next Header field, which is the Protocol Number of
    /// the upper-layer payload.
    ///
    /// The returned body will be [`MaybeParsed::Complete`] if all the body
    /// bytes were consumed (as stated by the header's payload length value) or
    /// [`MaybeParsed::Incomplete`] containing the bytes that were present
    /// otherwise.
    pub fn body_proto(&self) -> Result<(MaybeParsed<&[u8], &[u8]>, Ipv6Proto), ExtHdrParseError> {
        self.body_proto
            .as_ref()
            .map(|(mp, proto)| {
                (mp.as_ref().map(|b| b.deref()).map_incomplete(|b| b.deref()), *proto)
            })
            .map_err(|e| *e)
    }

    /// Returns the body.
    ///
    /// If extension headers failed to parse, `body` returns
    /// `Err(ExtHdrParseError)`. Extension headers must be parsed in order to
    /// find the bounds of the upper-layer payload.
    ///
    /// The returned body will be [`MaybeParsed::Complete`] if all the body
    /// bytes were consumed (as stated by the header's payload length value) or
    /// [`MaybeParsed::Incomplete`] containing the bytes that were present
    /// otherwise.
    pub fn body(&self) -> Result<MaybeParsed<&[u8], &[u8]>, ExtHdrParseError> {
        self.body_proto().map(|(body, _proto)| body)
    }

    /// Returns the upper-layer Protocol Number.
    ///
    /// If extension headers failed to parse, `body_proto` returns
    /// `Err(ExtHdrParseError)`. Extension headers must be parsed in order to
    /// find the last extension header's Next Header field, which is the
    /// Protocol Number of the upper-layer payload.
    pub fn proto(&self) -> Result<Ipv6Proto, ExtHdrParseError> {
        self.body_proto().map(|(_body, proto)| proto)
    }
}

/// A builder for IPv6 packets.
#[derive(Debug, Clone, Eq, PartialEq)]
pub struct Ipv6PacketBuilder {
    ds: u8,
    ecn: u8,
    flowlabel: u32,
    hop_limit: u8,
    // The protocol number of the upper layer protocol, not the Next Header
    // value of the first extension header (if one exists).
    proto: Ipv6Proto,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
}

impl Ipv6PacketBuilder {
    /// Constructs a new `Ipv6PacketBuilder`.
    ///
    /// The `proto` field encodes the protocol number identifying the upper
    /// layer payload, not the Next Header value of the first extension header
    /// (if one exists).
    pub fn new<S: Into<Ipv6Addr>, D: Into<Ipv6Addr>>(
        src_ip: S,
        dst_ip: D,
        hop_limit: u8,
        proto: Ipv6Proto,
    ) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: 0,
            ecn: 0,
            flowlabel: 0,
            hop_limit,
            proto,
            src_ip: src_ip.into(),
            dst_ip: dst_ip.into(),
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
}

/// A builder for Ipv6 packets with HBH Options.
#[derive(Debug)]
pub struct Ipv6PacketBuilderWithHbhOptions<'a, I> {
    prefix_builder: Ipv6PacketBuilder,
    hbh_options: AlignedRecordSequenceBuilder<HopByHopOption<'a>, I>,
}

impl<'a, I> Ipv6PacketBuilderWithHbhOptions<'a, I>
where
    I: Iterator + Clone,
    I::Item: Borrow<HopByHopOption<'a>>,
{
    /// Creates a IPv6 packet builder with a Hop By Hop Options extension header.
    pub fn new<T: IntoIterator<Item = I::Item, IntoIter = I>>(
        prefix_builder: Ipv6PacketBuilder,
        options: T,
    ) -> Option<Ipv6PacketBuilderWithHbhOptions<'a, I>> {
        let iter = options.into_iter();
        // https://tools.ietf.org/html/rfc2711#section-2.1 specifies that
        // an RouterAlert option can only appear once.
        if iter
            .clone()
            .filter(|r| matches!(r.borrow().data, HopByHopOptionData::RouterAlert { .. }))
            .count()
            > 1
        {
            return None;
        }
        let hbh_options = AlignedRecordSequenceBuilder::new(2, iter);
        // And we don't want our options to become too long.
        if next_multiple_of_eight(2 + hbh_options.serialized_len()) > IPV6_HBH_OPTIONS_MAX_LEN {
            return None;
        }
        Some(Ipv6PacketBuilderWithHbhOptions { prefix_builder, hbh_options })
    }

    fn aligned_hbh_len(&self) -> usize {
        let opt_len = self.hbh_options.serialized_len();
        let hbh_len = opt_len + 2;
        next_multiple_of_eight(hbh_len)
    }
}

fn next_multiple_of_eight(x: usize) -> usize {
    (x + 7) & (!7)
}

impl Ipv6PacketBuilder {
    fn serialize_fixed_hdr<B: ByteSliceMut, BV: BufferViewMut<B>>(
        &self,
        mut buffer: BV,
        payload_len: usize,
        next_hdr: u8,
    ) {
        buffer
            .write_obj_front(&FixedHeader::new(
                self.ds,
                self.ecn,
                self.flowlabel,
                {
                    // The caller promises to supply a body whose length
                    // does not exceed max_body_len. Doing this as a
                    // debug_assert (rather than an assert) is fine because,
                    // with debug assertions disabled, we'll just write an
                    // incorrect header value, which is acceptable if the
                    // caller has violated their contract.
                    debug_assert!(payload_len <= core::u16::MAX as usize);
                    payload_len as u16
                },
                next_hdr,
                self.hop_limit,
                self.src_ip,
                self.dst_ip,
            ))
            .expect("not enough bytes for IPv6 fixed header");
    }
}

impl PacketBuilder for Ipv6PacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        // TODO(joshlf): Update when we support serializing extension headers
        PacketConstraints::new(IPV6_FIXED_HDR_LEN, 0, 0, (1 << 16) - 1)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, body, _) = buffer.parts();
        self.serialize_fixed_hdr(&mut header, body.len(), self.proto.into());
    }
}

impl IpPacketBuilder<Ipv6> for Ipv6PacketBuilder {
    fn new(src_ip: Ipv6Addr, dst_ip: Ipv6Addr, ttl: u8, proto: Ipv6Proto) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(src_ip, dst_ip, ttl, proto)
    }

    fn src_ip(&self) -> Ipv6Addr {
        self.src_ip
    }

    fn dst_ip(&self) -> Ipv6Addr {
        self.dst_ip
    }
}

impl<'a, I> PacketBuilder for Ipv6PacketBuilderWithHbhOptions<'a, I>
where
    I: Iterator + Clone,
    I::Item: Borrow<HopByHopOption<'a>>,
{
    fn constraints(&self) -> PacketConstraints {
        let header_len = IPV6_FIXED_HDR_LEN + self.aligned_hbh_len();
        PacketConstraints::new(header_len, 0, 0, (1 << 16) - 1)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, body, _) = buffer.parts();
        let aligned_hbh_len = self.aligned_hbh_len();
        // The next header in the fixed header now should be 0 (Hop-by-Hop Extension Header)
        self.prefix_builder.serialize_fixed_hdr(&mut header, body.len() + aligned_hbh_len, 0);
        // header implements BufferViewMut
        let mut header = &mut header;
        let mut hbh_extension_header = header
            .take_back_zero(aligned_hbh_len)
            .expect("too few bytes for Hop-by-Hop extension header");
        let mut hbh_pointer = &mut hbh_extension_header;
        // take the first two bytes to write in proto and length information.
        let next_header_and_len = hbh_pointer.take_front_zero(2).unwrap();
        next_header_and_len[0] = self.prefix_builder.proto.into();
        next_header_and_len[1] =
            u8::try_from((aligned_hbh_len - 8) / 8).expect("extension header too big");
        // After the first two bytes, we can serialize our real options.
        let options = hbh_pointer.take_rest_front_zero();
        self.hbh_options.serialize_into(options);
    }
}

/// Reassembles a fragmented packet into a parsed IP packet.
pub(crate) fn reassemble_fragmented_packet<
    B: ByteSliceMut,
    BV: BufferViewMut<B>,
    I: Iterator<Item = Vec<u8>>,
>(
    mut buffer: BV,
    header: Vec<u8>,
    body_fragments: I,
) -> IpParseResult<Ipv6, Ipv6Packet<B>> {
    let bytes = buffer.as_mut();

    // First, copy over the header data.
    bytes[0..header.len()].copy_from_slice(&header[..]);
    let mut byte_count = header.len();

    // Next, copy over the body fragments.
    for p in body_fragments {
        bytes[byte_count..byte_count + p.len()].copy_from_slice(&p[..]);
        byte_count += p.len();
    }

    //
    // Fix up the IPv6 header
    //

    // For IPv6, the payload length is the sum of the length of the
    // extension headers and the packet body. The header as it is stored
    // includes the IPv6 fixed header and all extension headers, so
    // `bytes_count` is the sum of the size of the fixed header,
    // extension headers and packet body. To calculate the payload
    // length we subtract the size of the fixed header from the total
    // byte count of a reassembled packet.
    let payload_length = byte_count - IPV6_FIXED_HDR_LEN;

    // Make sure that the payload length is not more than the maximum
    // possible IPv4 packet length.
    if payload_length > usize::from(core::u16::MAX) {
        return debug_err!(
            Err(ParseError::Format.into()),
            "fragmented packet payload length of {} bytes is too large",
            payload_length
        );
    }

    // We know the call to `unwrap` will not fail because we just copied the header
    // bytes into `bytes`.
    let mut header = LayoutVerified::<_, FixedHeader>::new_unaligned_from_prefix(bytes).unwrap().0;

    // Update the payload length field.
    header.payload_len.set(u16::try_from(payload_length).unwrap());

    Ipv6Packet::parse_mut(buffer, ())
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;
    use packet::FragmentedBuffer;
    use packet::{Buf, InnerPacketBuilder, ParseBuffer, Serializer};

    use crate::ethernet::{EthernetFrame, EthernetFrameLengthCheck};
    use crate::ip::{IpProto, Ipv4Proto, Ipv6ExtHdrType};
    use crate::testutil::*;

    use super::ext_hdrs::*;
    use super::*;

    const DEFAULT_SRC_IP: Ipv6Addr =
        Ipv6Addr::from_bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const DEFAULT_DST_IP: Ipv6Addr =
        Ipv6Addr::from_bytes([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    const DEFAULT_V4_SRC_IP: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const DEFAULT_V4_DST_IP: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);

    #[test]
    fn test_parse_serialize_full_tcp() {
        use crate::testdata::syn_v6::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv6Packet<_>>().unwrap();
        verify_ipv6_packet(&packet, IPV6_PACKET);

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
        use crate::testdata::dns_request_v6::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let mut body = frame.body();
        let packet = body.parse::<Ipv6Packet<_>>().unwrap();
        verify_ipv6_packet(&packet, IPV6_PACKET);

        let buffer = packet
            .body()
            .into_serializer()
            .encapsulate(packet.builder())
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(buffer.as_ref(), ETHERNET_FRAME.bytes);
    }

    fn fixed_hdr_to_bytes(fixed_hdr: FixedHeader) -> [u8; IPV6_FIXED_HDR_LEN] {
        let mut bytes = [0; IPV6_FIXED_HDR_LEN];
        {
            let mut lv = LayoutVerified::<_, FixedHeader>::new_unaligned(&mut bytes[..]).unwrap();
            *lv = fixed_hdr;
        }
        bytes
    }

    // Return a new FixedHeader with reasonable defaults.
    fn new_fixed_hdr() -> FixedHeader {
        FixedHeader::new(0, 2, 0x77, 0, IpProto::Tcp.into(), 64, DEFAULT_SRC_IP, DEFAULT_DST_IP)
    }

    #[test]
    fn test_parse() {
        let mut buf = &fixed_hdr_to_bytes(new_fixed_hdr())[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        assert_eq!(packet.ds(), 0);
        assert_eq!(packet.ecn(), 2);
        assert_eq!(packet.flowlabel(), 0x77);
        assert_eq!(packet.hop_limit(), 64);
        assert_eq!(packet.fixed_hdr.next_hdr, IpProto::Tcp.into());
        assert_eq!(packet.proto(), IpProto::Tcp.into());
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), []);
    }

    #[test]
    fn test_parse_with_ext_hdrs() {
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Routing.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Routing Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type (Deprecated as per RFC 5095)
            0,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        assert_eq!(packet.ds(), 0);
        assert_eq!(packet.ecn(), 2);
        assert_eq!(packet.flowlabel(), 0x77);
        assert_eq!(packet.hop_limit(), 64);
        assert_eq!(packet.fixed_hdr.next_hdr, Ipv6ExtHdrType::HopByHopOptions.into());
        assert_eq!(packet.proto(), IpProto::Tcp.into());
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), [1, 2, 3, 4, 5]);
        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = packet.iter_extension_hdrs().collect();
        assert_eq!(ext_hdrs.len(), 2);
        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, Ipv6ExtHdrType::Routing.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched HopByHopOptions!");
        }

        // Note the second extension header (routing) should have been skipped because
        // it's routing type is unrecognized, but segments left is 0.

        // Check the third extension header (destination options)
        assert_eq!(ext_hdrs[1].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[1].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched DestinationOptions!");
        }

        // Test with a NoNextHeader as the final Next Header
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header w/ NoNextHeader as the next header
            Ipv6Proto::NoNextHeader.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        assert_eq!(packet.ds(), 0);
        assert_eq!(packet.ecn(), 2);
        assert_eq!(packet.flowlabel(), 0x77);
        assert_eq!(packet.hop_limit(), 64);
        assert_eq!(packet.fixed_hdr.next_hdr, Ipv6ExtHdrType::HopByHopOptions.into());
        assert_eq!(packet.proto(), Ipv6Proto::NoNextHeader);
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), [1, 2, 3, 4, 5]);
        let ext_hdrs: Vec<Ipv6ExtensionHeader<'_>> = packet.iter_extension_hdrs().collect();
        assert_eq!(ext_hdrs.len(), 1);
        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, Ipv6Proto::NoNextHeader.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched HopByHopOptions!");
        }
    }

    #[test]
    fn test_parse_error() {
        // Set the version to 5. The version must be 6.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.version_tc_flowlabel[0] = 0x50;
        assert_eq!(
            (&fixed_hdr_to_bytes(fixed_hdr)[..]).parse::<Ipv6Packet<_>>().unwrap_err(),
            ParseError::Format.into()
        );

        // Set the payload len to 2, even though there's no payload.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.payload_len = U16::new(2);
        assert_eq!(
            (&fixed_hdr_to_bytes(fixed_hdr)[..]).parse::<Ipv6Packet<_>>().unwrap_err(),
            ParseError::Format.into()
        );

        // Use invalid next header.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = 255;
        assert_eq!(
            (&fixed_hdr_to_bytes(fixed_hdr)[..]).parse::<Ipv6Packet<_>>().unwrap_err(),
            IpParseError::ParameterProblem {
                src_ip: DEFAULT_SRC_IP,
                dst_ip: DEFAULT_DST_IP,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer: u32::from(NEXT_HEADER_OFFSET),
                must_send_icmp: false,
                header_len: (),
                action: IpParseErrorAction::DiscardPacketSendIcmpNoMulticast,
            }
        );

        // Use ICMP(v4) as next header.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv4Proto::Icmp.into();
        assert_eq!(
            (&fixed_hdr_to_bytes(fixed_hdr)[..]).parse::<Ipv6Packet<_>>().unwrap_err(),
            IpParseError::ParameterProblem {
                src_ip: DEFAULT_SRC_IP,
                dst_ip: DEFAULT_DST_IP,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer: u32::from(NEXT_HEADER_OFFSET),
                must_send_icmp: false,
                header_len: (),
                action: IpParseErrorAction::DiscardPacketSendIcmpNoMulticast,
            }
        );

        // Test HopByHop extension header not being the very first extension header
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Routing Extension Header
            Ipv6ExtHdrType::HopByHopOptions.into(),    // Next Header (Valid but HopByHop restricted to first extension header)
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type
            0,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // HopByHop Options Extension Header
            IpProto::Tcp.into(),             // Next Header
            0,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Pad1
            1, 0,                               // Pad2
            1, 1, 0,                            // Pad3

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Routing.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        assert_eq!(
            buf.parse::<Ipv6Packet<_>>().unwrap_err(),
            IpParseError::ParameterProblem {
                src_ip: DEFAULT_SRC_IP,
                dst_ip: DEFAULT_DST_IP,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer: IPV6_FIXED_HDR_LEN as u32,
                must_send_icmp: false,
                header_len: (),
                action: IpParseErrorAction::DiscardPacketSendIcmpNoMulticast,
            }
        );

        // Test Unrecognized Routing Type
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Routing Extension Header
            IpProto::Tcp.into(),                // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            255,                                // Routing Type (Invalid)
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Routing.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        assert_eq!(
            buf.parse::<Ipv6Packet<_>>().unwrap_err(),
            IpParseError::ParameterProblem {
                src_ip: DEFAULT_SRC_IP,
                dst_ip: DEFAULT_DST_IP,
                code: Icmpv6ParameterProblemCode::ErroneousHeaderField,
                pointer: (IPV6_FIXED_HDR_LEN as u32) + 2,
                must_send_icmp: true,
                header_len: (),
                action: IpParseErrorAction::DiscardPacketSendIcmpNoMulticast,
            }
        );
    }

    #[test]
    fn test_parse_all_next_header_values() {
        // Test that, when parsing a packet with the fixed header's Next Header
        // field set to any value, parsing does not panic. A previous version
        // of this code would panic on some Next Header values.

        // This packet was found via fuzzing to trigger a panic.
        let mut buf = [
            0x81, 0x13, 0x27, 0xeb, 0x75, 0x92, 0x33, 0x89, 0x01, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
            0x03, 0x70, 0x00, 0x22, 0xf7, 0x30, 0x2c, 0x06, 0xfe, 0xc9, 0x00, 0x2d, 0x3b, 0xeb,
            0xad, 0x3e, 0x5c, 0x41, 0xc8, 0x70, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf6, 0x11, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x4f, 0x4f, 0x6f, 0x4f, 0x4f, 0x4f, 0x4f,
            0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x4f, 0x19, 0x19,
            0x19, 0x19, 0x19, 0x4f, 0x4f, 0x4f, 0x4f, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x4f, 0x4f, 0x5a, 0x5a, 0x5a, 0xc9, 0x5a, 0x46, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
            0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0xe4, 0x5a, 0x5a, 0x5a, 0x5a,
        ];

        // First, assert that the Next Header value found by the fuzzer (51)
        // produces the error we expect.
        assert_matches!(
            (&buf[..]).parse::<Ipv6Packet<_>>(),
            Err(IpParseError::ParameterProblem {
                src_ip: _,
                dst_ip: _,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer: 0,
                must_send_icmp: false,
                header_len: (),
                action: IpParseErrorAction::DiscardPacket,
            })
        );

        // Second, ensure that, regardless of the exact result produced, no Next
        // Header value causes parsing to panic.
        for b in 0u8..=255 {
            // Overwrite the Next Header field.
            buf[6] = b;
            let _: Result<_, _> = (&buf[..]).parse::<Ipv6Packet<_>>();
        }
    }

    #[test]
    fn test_partial_parse() {
        use core::convert::TryInto as _;
        use core::ops::Deref as _;

        // Can't partial parse extension headers:
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            IpProto::Tcp.into(), // Next Header
            0,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                   // Pad1
            1, 0,                // Pad2
            1, 1, 0,             // Pad3

            // Body
            1, 2, 3, 4, 5,
        ];
        let len = buf.len() - IPV6_FIXED_HDR_LEN;
        let len = len.try_into().unwrap();
        let make_fixed_hdr = || {
            let mut fixed_hdr = new_fixed_hdr();
            fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
            fixed_hdr.payload_len = U16::new(len);
            fixed_hdr
        };
        // make HopByHop malformed:
        const MALFORMED_BYTE: u8 = 10;
        buf[IPV6_FIXED_HDR_LEN + 1] = MALFORMED_BYTE;
        let fixed_hdr = fixed_hdr_to_bytes(make_fixed_hdr());
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr);
        let mut buf = &buf[..];
        let partial = buf.parse::<Ipv6PacketRaw<_>>().unwrap();
        let Ipv6PacketRaw { fixed_hdr, extension_hdrs, body_proto } = &partial;
        assert_eq!(fixed_hdr.deref(), &make_fixed_hdr());
        assert_eq!(
            extension_hdrs.as_ref().incomplete().unwrap().deref(),
            [IpProto::Tcp.into(), MALFORMED_BYTE]
        );
        assert_eq!(body_proto, &Err(ExtHdrParseError));
        assert!(Ipv6Packet::try_from_raw(partial).is_err());

        // Incomplete body:
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Body
            1, 2, 3, 4, 5,
        ];
        let make_fixed_hdr = || {
            let mut fixed_hdr = new_fixed_hdr();
            fixed_hdr.next_hdr = IpProto::Tcp.into();
            fixed_hdr.payload_len = U16::new(10);
            fixed_hdr
        };
        let fixed_hdr = fixed_hdr_to_bytes(make_fixed_hdr());
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr);
        let mut parsebuff = &buf[..];
        let partial = parsebuff.parse::<Ipv6PacketRaw<_>>().unwrap();
        let Ipv6PacketRaw { fixed_hdr, extension_hdrs, body_proto } = &partial;
        assert_eq!(fixed_hdr.deref(), &make_fixed_hdr());
        assert_eq!(extension_hdrs.as_ref().complete().unwrap().deref(), []);
        let (body, proto) = body_proto.unwrap();
        assert_eq!(body.incomplete().unwrap(), &buf[IPV6_FIXED_HDR_LEN..]);
        assert_eq!(proto, IpProto::Tcp.into());
        assert!(Ipv6Packet::try_from_raw(partial).is_err());
    }

    // Return a stock Ipv6PacketBuilder with reasonable default values.
    fn new_builder() -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(DEFAULT_SRC_IP, DEFAULT_DST_IP, 64, IpProto::Tcp.into())
    }

    #[test]
    fn test_serialize() {
        let mut builder = new_builder();
        builder.ds(0x12);
        builder.ecn(3);
        builder.flowlabel(0x10405);
        let mut buf = (&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
            .into_serializer()
            .encapsulate(builder)
            .serialize_vec_outer()
            .unwrap();
        // assert that we get the literal bytes we expected
        assert_eq!(
            buf.as_ref(),
            &[
                100, 177, 4, 5, 0, 10, 6, 64, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 0, 1, 2, 3, 4,
                5, 6, 7, 8, 9
            ][..],
        );

        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        // assert that when we parse those bytes, we get the values we set in
        // the builder
        assert_eq!(packet.ds(), 0x12);
        assert_eq!(packet.ecn(), 3);
        assert_eq!(packet.flowlabel(), 0x10405);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that Ipv6PacketBuilder::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; IPV6_FIXED_HDR_LEN];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_0[..], IPV6_FIXED_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        let mut buf_1 = [0xFF; IPV6_FIXED_HDR_LEN];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_1[..], IPV6_FIXED_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    fn test_packet_builder_proto_not_next_header() {
        // Test that Ipv6PacketBuilder's `proto` field is used as the Protocol
        // Number for the upper layer payload, not the Next Header value for the
        // extension header.
        let mut buf = (&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
            .into_serializer()
            .encapsulate(
                Ipv6PacketBuilderWithHbhOptions::new(
                    new_builder(),
                    &[HopByHopOption {
                        action: ExtensionHeaderOptionAction::SkipAndContinue,
                        mutable: false,
                        data: HopByHopOptionData::RouterAlert { data: 0 },
                    }],
                )
                .unwrap(),
            )
            .serialize_vec_outer()
            .unwrap();
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        assert_eq!(packet.proto(), IpProto::Tcp.into());
        assert_eq!(packet.next_header(), Ipv6ExtHdrType::HopByHopOptions.into());
    }

    #[test]
    #[should_panic(expected = "Mtu, Nested { inner: Buf { buf:")]
    fn test_serialize_panic_packet_length() {
        // Test that a packet whose payload is longer than 2^16 - 1 bytes is
        // rejected.
        let _: Buf<&mut [u8]> = Buf::new(&mut [0; 1 << 16][..], ..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
    }

    #[test]
    #[should_panic(expected = "packet must have at least one extension header")]
    fn test_copy_header_bytes_for_fragment_without_ext_hdrs() {
        let mut buf = &fixed_hdr_to_bytes(new_fixed_hdr())[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let _: Vec<_> = packet.copy_header_bytes_for_fragment();
    }

    #[test]
    #[should_panic(expected = "exhausted all extension headers without finding fragment header")]
    fn test_copy_header_bytes_for_fragment_with_1_ext_hdr_no_fragment() {
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            IpProto::Tcp.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let _: Vec<_> = packet.copy_header_bytes_for_fragment();
    }

    #[test]
    #[should_panic(expected = "exhausted all extension headers without finding fragment header")]
    fn test_copy_header_bytes_for_fragment_with_2_ext_hdr_no_fragment() {
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let _: Vec<_> = packet.copy_header_bytes_for_fragment();
    }

    #[test]
    fn test_copy_header_bytes_for_fragment() {
        //
        // Only a fragment extension header
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Fragment Extension Header
            IpProto::Tcp.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Fragment.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        bytes[6] = IpProto::Tcp.into();
        assert_eq!(&copied_bytes[..], &bytes[..IPV6_FIXED_HDR_LEN]);

        //
        // Fragment header after a single extension header
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Fragment.into(),    // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Fragment Extension Header
            IpProto::Tcp.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        bytes[IPV6_FIXED_HDR_LEN] = IpProto::Tcp.into();
        assert_eq!(&copied_bytes[..], &bytes[..IPV6_FIXED_HDR_LEN + 8]);

        //
        // Fragment header after many extension headers (many = 2)
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Destination Options Extension Header
            Ipv6ExtHdrType::Fragment.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Fragment Extension Header
            IpProto::Tcp.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        bytes[IPV6_FIXED_HDR_LEN + 8] = IpProto::Tcp.into();
        assert_eq!(&copied_bytes[..], &bytes[..IPV6_FIXED_HDR_LEN + 24]);

        //
        // Fragment header before an extension header
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Fragment Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Fragment.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        let mut expected_bytes = Vec::new();
        expected_bytes.extend_from_slice(&bytes[..IPV6_FIXED_HDR_LEN]);
        expected_bytes.extend_from_slice(&bytes[IPV6_FIXED_HDR_LEN + 8..bytes.len() - 5]);
        expected_bytes[6] = Ipv6ExtHdrType::DestinationOptions.into();
        assert_eq!(&copied_bytes[..], &expected_bytes[..]);

        //
        // Fragment header before many extension headers (many = 2)
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Fragment Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Destination Options Extension Header
            Ipv6ExtHdrType::Routing.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Routing extension header
            IpProto::Tcp.into(),                // Next Header
            4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Routing Type (Deprecated as per RFC 5095)
            0,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Fragment.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        let mut expected_bytes = Vec::new();
        expected_bytes.extend_from_slice(&bytes[..IPV6_FIXED_HDR_LEN]);
        expected_bytes.extend_from_slice(&bytes[IPV6_FIXED_HDR_LEN + 8..bytes.len() - 5]);
        expected_bytes[6] = Ipv6ExtHdrType::DestinationOptions.into();
        assert_eq!(&copied_bytes[..], &expected_bytes[..]);

        //
        // Fragment header between extension headers
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::Fragment.into(),    // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                       // Pad1
            1, 0,                    // Pad2
            1, 1, 0,                 // Pad3

            // Fragment Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Destination Options Extension Header
            IpProto::Tcp.into(),    // Next Header
            1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                      // Pad1
            1, 0,                   // Pad2
            1, 1, 0,                // Pad3
            1, 6, 0, 0, 0, 0, 0, 0, // Pad8

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        let mut expected_bytes = Vec::new();
        expected_bytes.extend_from_slice(&bytes[..IPV6_FIXED_HDR_LEN + 8]);
        expected_bytes.extend_from_slice(&bytes[IPV6_FIXED_HDR_LEN + 16..bytes.len() - 5]);
        expected_bytes[IPV6_FIXED_HDR_LEN] = Ipv6ExtHdrType::DestinationOptions.into();
        assert_eq!(&copied_bytes[..], &expected_bytes[..]);

        //
        // Multiple fragment extension headers
        //

        #[rustfmt::skip]
        let mut bytes = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // Fragment Extension Header
            Ipv6ExtHdrType::Fragment.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            1, 1, 1, 1,              // Identification

            // Fragment Extension Header
            IpProto::Tcp.into(),     // Next Header
            0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0, 0,                    // Fragment Offset, Res, M (M_flag)
            2, 2, 2, 2,              // Identification

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Fragment.into();
        fixed_hdr.payload_len = U16::new((bytes.len() - IPV6_FIXED_HDR_LEN) as u16);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        bytes[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &bytes[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        let copied_bytes = packet.copy_header_bytes_for_fragment();
        let mut expected_bytes = Vec::new();
        expected_bytes.extend_from_slice(&bytes[..IPV6_FIXED_HDR_LEN]);
        expected_bytes.extend_from_slice(&bytes[IPV6_FIXED_HDR_LEN + 8..bytes.len() - 5]);
        assert_eq!(&copied_bytes[..], &expected_bytes[..]);
    }

    #[test]
    fn test_next_multiple_of_eight() {
        for x in 0usize..=IPV6_HBH_OPTIONS_MAX_LEN {
            let y = next_multiple_of_eight(x);
            assert_eq!(y % 8, 0);
            assert!(y >= x);
            if x % 8 == 0 {
                assert_eq!(x, y);
            } else {
                assert_eq!(x + (8 - x % 8), y);
            }
        }
    }

    fn create_ipv4_and_ipv6_builders(
        proto_v4: Ipv4Proto,
        proto_v6: Ipv6Proto,
    ) -> (Ipv4PacketBuilder, Ipv6PacketBuilder) {
        const IP_DSCP: u8 = 0x12;
        const IP_ECN: u8 = 3;
        const IP_TTL: u8 = 64;

        let mut ipv4_builder =
            Ipv4PacketBuilder::new(DEFAULT_V4_SRC_IP, DEFAULT_V4_DST_IP, IP_TTL, proto_v4);
        ipv4_builder.dscp(IP_DSCP);
        ipv4_builder.ecn(IP_ECN);
        ipv4_builder.df_flag(false);
        ipv4_builder.mf_flag(false);
        ipv4_builder.fragment_offset(0);

        let mut ipv6_builder =
            Ipv6PacketBuilder::new(DEFAULT_SRC_IP, DEFAULT_DST_IP, IP_TTL, proto_v6);
        ipv6_builder.ds(IP_DSCP);
        ipv6_builder.ecn(IP_ECN);
        ipv6_builder.flowlabel(0x456);

        (ipv4_builder, ipv6_builder)
    }

    fn create_tcp_ipv4_and_ipv6_pkt(
    ) -> (packet::Either<EmptyBuf, Buf<Vec<u8>>>, packet::Either<EmptyBuf, Buf<Vec<u8>>>) {
        use crate::tcp::TcpSegmentBuilder;
        use core::num::NonZeroU16;

        let tcp_src_port: NonZeroU16 = NonZeroU16::new(20).unwrap();
        let tcp_dst_port: NonZeroU16 = NonZeroU16::new(30).unwrap();
        const TCP_SEQ_NUM: u32 = 4321;
        const TCP_ACK_NUM: Option<u32> = Some(1234);
        const TCP_WINDOW_SIZE: u16 = 12345;
        const PAYLOAD: [u8; 10] = [0, 1, 2, 3, 3, 4, 5, 7, 8, 9];

        let (ipv4_builder, ipv6_builder) =
            create_ipv4_and_ipv6_builders(IpProto::Tcp.into(), IpProto::Tcp.into());

        let tcp_builder = TcpSegmentBuilder::new(
            DEFAULT_V4_SRC_IP,
            DEFAULT_V4_DST_IP,
            tcp_src_port,
            tcp_dst_port,
            TCP_SEQ_NUM,
            TCP_ACK_NUM,
            TCP_WINDOW_SIZE,
        );

        let v4_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(tcp_builder)
            .encapsulate(ipv4_builder)
            .serialize_vec_outer()
            .expect("Failed to serialize to v4_pkt_buf");

        let v6_tcp_builder = TcpSegmentBuilder::new(
            DEFAULT_SRC_IP,
            DEFAULT_DST_IP,
            tcp_src_port,
            tcp_dst_port,
            TCP_SEQ_NUM,
            TCP_ACK_NUM,
            TCP_WINDOW_SIZE,
        );

        let v6_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(v6_tcp_builder)
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .expect("Failed to serialize to v4_pkt_buf");

        (v4_pkt_buf, v6_pkt_buf)
    }

    #[test]
    fn test_nat64_translate_tcp() {
        let (expected_v4_pkt_buf, mut v6_pkt_buf) = create_tcp_ipv4_and_ipv6_pkt();

        let parsed_v6_packet =
            v6_pkt_buf.parse::<Ipv6Packet<_>>().expect("Failed to parse v6_pkt_buf");
        let nat64_translation_result =
            parsed_v6_packet.nat64_translate(DEFAULT_V4_SRC_IP, DEFAULT_V4_DST_IP);

        let serializable_pkt =
            assert_matches!(nat64_translation_result, Nat64TranslationResult::Forward(s) => s);

        let translated_v4_pkt_buf = serializable_pkt
            .serialize_vec_outer()
            .expect("Failed to serialize to translated_v4_pkt_buf");

        assert_eq!(
            expected_v4_pkt_buf.to_flattened_vec(),
            translated_v4_pkt_buf.to_flattened_vec()
        );
    }

    fn create_udp_ipv4_and_ipv6_pkt(
    ) -> (packet::Either<EmptyBuf, Buf<Vec<u8>>>, packet::Either<EmptyBuf, Buf<Vec<u8>>>) {
        use crate::udp::UdpPacketBuilder;
        use core::num::NonZeroU16;

        let udp_src_port: NonZeroU16 = NonZeroU16::new(35000).unwrap();
        let udp_dst_port: NonZeroU16 = NonZeroU16::new(53).unwrap();
        const PAYLOAD: [u8; 10] = [0, 1, 2, 3, 3, 4, 5, 7, 8, 9];

        let (ipv4_builder, ipv6_builder) =
            create_ipv4_and_ipv6_builders(IpProto::Udp.into(), IpProto::Udp.into());

        let v4_udp_builder = UdpPacketBuilder::new(
            DEFAULT_V4_SRC_IP,
            DEFAULT_V4_DST_IP,
            Some(udp_src_port),
            udp_dst_port,
        );

        let v4_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(v4_udp_builder)
            .encapsulate(ipv4_builder)
            .serialize_vec_outer()
            .expect("Unable to serialize to v4_pkt_buf");

        let v6_udp_builder =
            UdpPacketBuilder::new(DEFAULT_SRC_IP, DEFAULT_DST_IP, Some(udp_src_port), udp_dst_port);

        let v6_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(v6_udp_builder)
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .expect("Unable to serialize to v6_pkt_buf");

        (v4_pkt_buf, v6_pkt_buf)
    }

    #[test]
    fn test_nat64_translate_udp() {
        let (expected_v4_pkt_buf, mut v6_pkt_buf) = create_udp_ipv4_and_ipv6_pkt();

        let parsed_v6_packet =
            v6_pkt_buf.parse::<Ipv6Packet<_>>().expect("Unable to parse Ipv6Packet");
        let nat64_translation_result =
            parsed_v6_packet.nat64_translate(DEFAULT_V4_SRC_IP, DEFAULT_V4_DST_IP);

        let serializable_pkt = assert_matches!(nat64_translation_result,
                                               Nat64TranslationResult::Forward(s) => s);

        let translated_v4_pkt_buf = serializable_pkt
            .serialize_vec_outer()
            .expect("Unable to serialize to translated_v4_pkt_buf");

        assert_eq!(
            expected_v4_pkt_buf.to_flattened_vec(),
            translated_v4_pkt_buf.to_flattened_vec()
        );
    }

    #[test]
    fn test_nat64_translate_non_tcp_udp_icmp() {
        const PAYLOAD: [u8; 10] = [0, 1, 2, 3, 3, 4, 5, 7, 8, 9];

        let (ipv4_builder, ipv6_builder) =
            create_ipv4_and_ipv6_builders(Ipv4Proto::Other(59), Ipv6Proto::Other(59));

        let expected_v4_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(ipv4_builder)
            .serialize_vec_outer()
            .expect("Unable to serialize to expected_v4_pkt_buf");

        let mut v6_pkt_buf = (&PAYLOAD)
            .into_serializer()
            .encapsulate(ipv6_builder)
            .serialize_vec_outer()
            .expect("Unable to serialize to v6_pkt_buf");

        let translated_v4_pkt_buf = {
            let parsed_v6_packet = v6_pkt_buf
                .parse::<Ipv6Packet<_>>()
                .expect("Unable to serialize to translated_v4_pkt_buf");

            let nat64_translation_result =
                parsed_v6_packet.nat64_translate(DEFAULT_V4_SRC_IP, DEFAULT_V4_DST_IP);

            let serializable_pkt = assert_matches!(nat64_translation_result,
                                                   Nat64TranslationResult::Forward(s) => s);

            let translated_buf = serializable_pkt
                .serialize_vec_outer()
                .expect("Unable to serialize to translated_buf");

            translated_buf
        };

        assert_eq!(
            expected_v4_pkt_buf.to_flattened_vec(),
            translated_v4_pkt_buf.to_flattened_vec()
        );
    }
}
