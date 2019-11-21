// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 packets.

pub(crate) mod ext_hdrs;

use std::convert::TryFrom;
use std::fmt::{self, Debug, Formatter};
use std::ops::Range;

use log::debug;
use net_types::ip::{Ipv6, Ipv6Addr};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
    SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::{IpParseError, IpParseErrorAction, IpParseResult, ParseError};
use crate::ip::reassembly::FragmentablePacket;
use crate::ip::{IpProto, Ipv6ExtHdrType};
use crate::wire::icmp::Icmpv6ParameterProblemCode;
use crate::wire::ipv6::ext_hdrs::{HopByHopOption, HopByHopOptionData, HopByHopOptionsImpl};
use crate::wire::records::{AlignedRecordsSerializer, Records, RecordsRaw};
use crate::wire::{FromRaw, MaybeParsed, U16};

use ext_hdrs::{
    is_valid_next_header, is_valid_next_header_upper_layer, ExtensionHeaderOptionAction,
    Ipv6ExtensionHeader, Ipv6ExtensionHeaderData, Ipv6ExtensionHeaderImpl,
    Ipv6ExtensionHeaderParsingContext, Ipv6ExtensionHeaderParsingError, IPV6_FRAGMENT_EXT_HDR_LEN,
};

/// Length of the IPv6 fixed header.
pub(crate) const IPV6_FIXED_HDR_LEN: usize = 40;

/// The range of bytes within an IPv6 header buffer that the
/// payload length field uses.
pub(crate) const IPV6_PAYLOAD_LEN_BYTE_RANGE: Range<usize> = 4..6;

// Offset to the Next Header field within the fixed IPv6 header
const NEXT_HEADER_OFFSET: u8 = 6;

// The maximum length for Hop-by-Hop Options. The stored byte's maximum
// representable value is `std::u8::MAX` and it means the header has
// that many 8-octets, not including the first 8 octets.
const IPV6_HBH_OPTIONS_MAX_LEN: usize = (std::u8::MAX as usize) * 8 + 8;

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
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendICMPNoMulticast),
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
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendICMPNoMulticast),
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
                        ExtensionHeaderOptionAction::DiscardPacketSendICMP => {
                            IpParseErrorAction::DiscardPacketSendICMP
                        }
                        ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast => {
                            IpParseErrorAction::DiscardPacketSendICMPNoMulticast
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

#[allow(missing_docs)]
#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
pub(crate) struct FixedHeader {
    version_tc_flowlabel: [u8; 4],
    payload_len: U16,
    next_hdr: u8,
    hop_limit: u8,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
}

impl FixedHeader {
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
pub(crate) trait Ipv6Header {
    /// Gets a reference to the IPv6 [`FixedHeader`].
    fn get_fixed_header(&self) -> &FixedHeader;

    /// The Hop Limit.
    fn hop_limit(&self) -> u8 {
        self.get_fixed_header().hop_limit
    }

    /// The Next Header.
    ///
    /// `next_header` returns the `IpProto` from the next header field.
    fn next_header(&self) -> IpProto {
        IpProto::from(self.get_fixed_header().next_hdr)
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
pub(crate) struct Ipv6Packet<B> {
    fixed_hdr: LayoutVerified<B, FixedHeader>,
    extension_hdrs: Records<B, Ipv6ExtensionHeaderImpl>,
    body: B,
    proto: IpProto,
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

    fn parse<BV: BufferView<B>>(buffer: BV, args: ()) -> IpParseResult<Ipv6, Self> {
        Ipv6PacketRaw::parse(buffer, args).and_then(|r| Ipv6Packet::try_from_raw(r))
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
                    action: IpParseErrorAction::DiscardPacketSendICMPNoMulticast,
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

        // If extension headers parse sucessfully, then proto MUST be available,
        // in the raw form AND that it's a valid next header for upper layers.
        let proto = raw.proto.expect("Unable to retrieve IpProto from raw");
        debug_assert!(is_valid_next_header_upper_layer(proto.into()));

        let body = match raw.body {
            Ok(MaybeParsed::Complete(b)) => b,
            _ => {
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

        Ok(Ipv6Packet { fixed_hdr, extension_hdrs, body: body, proto })
    }
}

impl<B: ByteSlice> FragmentablePacket for Ipv6Packet<B> {
    fn fragment_data(&self) -> (u32, u16, bool) {
        for ext_hdr in self.iter_extension_hdrs() {
            if let Ipv6ExtensionHeaderData::Fragment { fragment_data } = ext_hdr.data() {
                return (
                    fragment_data.identification(),
                    fragment_data.fragment_offset(),
                    fragment_data.m_flag(),
                );
            }
        }

        unreachable!(
            "Should never call this function if the packet does not have a fragment header"
        );
    }
}

impl<B: ByteSlice> Ipv6Packet<B> {
    pub(crate) fn iter_extension_hdrs<'a>(
        &'a self,
    ) -> impl 'a + Iterator<Item = Ipv6ExtensionHeader> {
        self.extension_hdrs.iter()
    }

    /// The packet body.
    pub(crate) fn body(&self) -> &[u8] {
        &self.body
    }

    /// The Differentiated Services (DS) field.
    pub(crate) fn ds(&self) -> u8 {
        self.fixed_hdr.ds()
    }

    /// The Explicit Congestion Notification (ECN).
    pub(crate) fn ecn(&self) -> u8 {
        self.fixed_hdr.ecn()
    }

    /// The flow label.
    pub(crate) fn flowlabel(&self) -> u32 {
        self.fixed_hdr.flowlabel()
    }

    /// The hop limit.
    pub(crate) fn hop_limit(&self) -> u8 {
        self.fixed_hdr.hop_limit
    }

    /// The Upper layer protocol for this packet.
    ///
    /// This is found in the fixed header's Next Header if there are no extension
    /// headers, or the Next Header value in the last extension header if there are.
    /// This also  uses the same codes, encoded by the Rust type `IpProto`.
    pub(crate) fn proto(&self) -> IpProto {
        self.proto
    }

    /// The source IP address.
    pub(crate) fn src_ip(&self) -> Ipv6Addr {
        self.fixed_hdr.src_ip
    }

    /// The destination IP address.
    pub(crate) fn dst_ip(&self) -> Ipv6Addr {
        self.fixed_hdr.dst_ip
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
    pub(crate) fn copy_header_bytes_for_fragment(&self) -> Vec<u8> {
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

    /// Construct a builder with the same contents as this packet.
    #[cfg(test)]
    pub(crate) fn builder(&self) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: self.ds(),
            ecn: self.ecn(),
            flowlabel: self.flowlabel(),
            hop_limit: self.hop_limit(),
            next_hdr: self.fixed_hdr.next_hdr,
            src_ip: self.src_ip(),
            dst_ip: self.dst_ip(),
        }
    }
}

impl<B: ByteSliceMut> Ipv6Packet<B> {
    /// Set the hop limit.
    pub(crate) fn set_hop_limit(&mut self, hlim: u8) {
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

/// We were unable to find the start of the body due to a malformed sequence of
/// extension headers.
///
/// Since we could not finish parsing extension headers, we were unable to
/// figure out where the body begins.
#[derive(Copy, Clone, Debug)]
pub(crate) struct UndefinedBodyBoundsError;

pub(crate) struct Ipv6PacketRaw<B> {
    /// A raw packet always contains at least a fully parsed `FixedHeader`.
    fixed_hdr: LayoutVerified<B, FixedHeader>,
    /// When `extension_hdrs` is [`MaybeParsed::Complete`], it contains the
    /// `RecordsRaw` that can be validated for full extension headers parsing.
    /// Otherwise, it just contains the extension header bytes that were
    /// successfully consumed before reaching an error (typically "buffer
    /// exhausted").
    extension_hdrs: MaybeParsed<RecordsRaw<B, Ipv6ExtensionHeaderImpl>, B>,
    /// If extension headers failed to parse, `body` will be
    /// `Err(UndefinedBodyBoundsError)`, since we can't find where the body
    /// begins. Otherwise it will be `Ok` of [`MaybeParsed::Complete`] if all
    /// the body bytes were consumed (as stated by the header's payload length
    /// value) or [`MaybeParsed::Incomplete`] containing the bytes that that
    /// were present otherwise.
    body: Result<MaybeParsed<B, B>, UndefinedBodyBoundsError>,
    /// If extension headers are successfully parsed, the last "next header"
    /// value is stored in `proto` as `Some(IpProto)`. Otherwise, `proto` will
    /// be `None`.
    proto: Option<IpProto>,
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
        let pl_len = usize::from(fixed_hdr.payload_len.get());
        if buffer.len() > pl_len {
            // get rid of any extra padding that may be at the end of the buffer
            // unwrapping is safe because of the check above.
            buffer.take_back(buffer.len() - pl_len).unwrap();
        }

        let mut extension_hdr_context = Ipv6ExtensionHeaderParsingContext::new(fixed_hdr.next_hdr);

        let extension_hdrs =
            RecordsRaw::parse_raw_with_mut_context(&mut buffer, &mut extension_hdr_context)
                .map_incomplete(|(b, _)| b);

        let (proto, body) = if extension_hdrs.is_complete() {
            // If we have extension headers our context's
            // (`extension_hdr_context`) `next_header` would be updated with the
            // last extension header's Next Header value. This will also work if
            // we don't have any extension headers. Let's consider that
            // scenario: When we have no extension headers, the Next Header
            // value in the fixed header will be a valid upper layer protocol
            // value. `parse_bv_with_mut_context` will return almost immediately
            // without doing any actual work when it checks the context's
            // (`extension_hdr_context`) `next_header`, value and ends parsing
            // since according to our context, its data is for an upper layer
            // protocol. Now, since nothing was parsed, our context was never
            // modified, so the next header value it was initialized with when
            // calling `Ipv6ExtensionHeaderParsingContext::new`, will not have
            // changed. We simply use that value and assign it to proto below.

            // Extension header raw parsing only finishes when we have a valid
            // next header that is meant for the upper layer. The assertion
            // below enforces that contract.
            assert!(is_valid_next_header_upper_layer(extension_hdr_context.next_header));
            let proto = Some(IpProto::from(extension_hdr_context.next_header));
            let body = MaybeParsed::new_with_min_len(
                buffer.into_rest(),
                pl_len.saturating_sub(extension_hdrs.len()),
            );
            (proto, Ok(body))
        } else {
            (None, Err(UndefinedBodyBoundsError))
        };

        Ok(Ipv6PacketRaw { fixed_hdr, extension_hdrs, body, proto })
    }

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.fixed_hdr.bytes().len() + self.extension_hdrs.len();
        let body_len = self.body.as_ref().map(|b| b.len()).unwrap_or(0);
        ParseMetadata::from_packet(header_len, body_len, 0)
    }
}

impl<B: ByteSlice> Ipv6PacketRaw<B> {
    /// Return the body.
    ///
    /// `body` returns [`Ok(MaybeParsed::Complete)`] if the entire body is
    /// present (as determined by the header's "payload length length" field),
    /// [`Ok(MaybeParsed::Incomplete)`] if only part of the body is present, or
    /// [`Err(UndefinedBodyBoundsError)`] if the packet's extension headers
    /// failed to parse (in which case we can't locate the body's beginning).
    pub(crate) fn body(&self) -> Result<MaybeParsed<&[u8], &[u8]>, UndefinedBodyBoundsError> {
        self.body
            .as_ref()
            .map(|mp| mp.as_ref().map(|b| b.deref()).map_incomplete(|b| b.deref()))
            .map_err(|e| *e)
    }
}

/// A builder for IPv6 packets.
#[derive(Debug, Clone, Eq, PartialEq)]
pub(crate) struct Ipv6PacketBuilder {
    ds: u8,
    ecn: u8,
    flowlabel: u32,
    hop_limit: u8,
    next_hdr: u8,
    src_ip: Ipv6Addr,
    dst_ip: Ipv6Addr,
}

impl Ipv6PacketBuilder {
    /// Construct a new `Ipv6PacketBuilder`.
    pub(crate) fn new<S: Into<Ipv6Addr>, D: Into<Ipv6Addr>>(
        src_ip: S,
        dst_ip: D,
        hop_limit: u8,
        next_hdr: IpProto,
    ) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: 0,
            ecn: 0,
            flowlabel: 0,
            hop_limit,
            next_hdr: next_hdr.into(),
            src_ip: src_ip.into(),
            dst_ip: dst_ip.into(),
        }
    }

    /// Set the Differentiated Services (DS).
    ///
    /// # Panics
    ///
    /// `ds` panics if `ds` is greater than 2^6 - 1.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn ds(&mut self, ds: u8) {
        assert!(ds <= 1 << 6, "invalid DS: {}", ds);
        self.ds = ds;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 0b11, "invalid ECN: {}", ecn);
        self.ecn = ecn
    }

    /// Set the flowlabel.
    ///
    /// # Panics
    ///
    /// `flowlabel` panics if `flowlabel` is greater than 2^20 - 1.
    // TODO(rheacock): remove `#[cfg(test)]` when this is used.
    #[cfg(test)]
    pub(crate) fn flowlabel(&mut self, flowlabel: u32) {
        assert!(flowlabel <= 1 << 20, "invalid flowlabel: {:x}", flowlabel);
        self.flowlabel = flowlabel;
    }
}

type OptionsSerializer<'a, I> =
    AlignedRecordsSerializer<'a, HopByHopOptionsImpl, HopByHopOption<'a>, I>;

/// A builder for Ipv6 packets with HBH Options.
#[derive(Debug)]
pub(crate) struct Ipv6PacketBuilderWithHBHOptions<
    'a,
    I: Clone + Iterator<Item = &'a HopByHopOption<'a>>,
> {
    prefix_builder: Ipv6PacketBuilder,
    hbh_options: OptionsSerializer<'a, I>,
}

impl<'a, I: Clone + Iterator<Item = &'a HopByHopOption<'a>>>
    Ipv6PacketBuilderWithHBHOptions<'a, I>
{
    pub(crate) fn new<T: IntoIterator<Item = I::Item, IntoIter = I>>(
        prefix_builder: Ipv6PacketBuilder,
        options: T,
    ) -> Option<Ipv6PacketBuilderWithHBHOptions<'a, I>> {
        let iter = options.into_iter();
        // https://tools.ietf.org/html/rfc2711#section-2.1 specifies that
        // an RouterAlert option can only appear once.
        if iter
            .clone()
            .filter(|r| match r.data {
                HopByHopOptionData::RouterAlert { .. } => true,
                _ => false,
            })
            .count()
            > 1
        {
            return None;
        }
        let hbh_options = OptionsSerializer::new(2, iter);
        // And we don't want our options to become too long.
        if next_multiple_of_eight(2 + hbh_options.records_bytes_len()) > IPV6_HBH_OPTIONS_MAX_LEN {
            return None;
        }
        Some(Ipv6PacketBuilderWithHBHOptions { prefix_builder, hbh_options })
    }

    fn aligned_hbh_len(&self) -> usize {
        let opt_len = self.hbh_options.records_bytes_len();
        let hbh_len = opt_len + 2;
        next_multiple_of_eight(hbh_len)
    }
}

fn next_multiple_of_eight(x: usize) -> usize {
    (x + 7) & (!7)
}

impl Ipv6PacketBuilder {
    fn serialize_fixed_hdr<B: ByteSliceMut>(
        &self,
        fixed_hdr: &mut LayoutVerified<B, FixedHeader>,
        payload_len: usize,
        next_hdr: u8,
    ) {
        fixed_hdr.version_tc_flowlabel = [
            (6u8 << 4) | self.ds >> 2,
            ((self.ds & 0b11) << 6) | (self.ecn << 4) | (self.flowlabel >> 16) as u8,
            ((self.flowlabel >> 8) & 0xFF) as u8,
            (self.flowlabel & 0xFF) as u8,
        ];
        // The caller promises to supply a body whose length does not exceed
        // max_body_len. Doing this as a debug_assert (rather than an assert) is
        // fine because, with debug assertions disabled, we'll just write an
        // incorrect header value, which is acceptable if the caller has
        // violated their contract.
        debug_assert!(payload_len <= std::u16::MAX as usize);
        let payload_len = payload_len as u16;
        fixed_hdr.payload_len = U16::new(payload_len);
        fixed_hdr.next_hdr = next_hdr;
        fixed_hdr.hop_limit = self.hop_limit;
        fixed_hdr.src_ip = self.src_ip;
        fixed_hdr.dst_ip = self.dst_ip;
    }
}

impl PacketBuilder for Ipv6PacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        // TODO(joshlf): Update when we support serializing extension headers
        PacketConstraints::new(IPV6_FIXED_HDR_LEN, 0, 0, (1 << 16) - 1)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // TODO(tkilbourn): support extension headers
        let mut fixed_hdr =
            header.take_obj_front_zero::<FixedHeader>().expect("too few bytes for IPv6 header");
        self.serialize_fixed_hdr(&mut fixed_hdr, body.len(), self.next_hdr);
    }
}

impl<'a, I: Clone + Iterator<Item = &'a HopByHopOption<'a>>> PacketBuilder
    for Ipv6PacketBuilderWithHBHOptions<'a, I>
{
    fn constraints(&self) -> PacketConstraints {
        let header_len = IPV6_FIXED_HDR_LEN + self.aligned_hbh_len();
        PacketConstraints::new(header_len, 0, 0, (1 << 16) - 1)
    }

    fn serialize(&self, buffer: &mut SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        let mut header = &mut header;

        let mut fixed_hdr =
            header.take_obj_front_zero::<FixedHeader>().expect("too few bytes for IPv6 header");
        let aligned_hbh_len = self.aligned_hbh_len();
        let mut hbh_extension_header = header
            .take_back_zero(aligned_hbh_len)
            .expect("too few bytes for Hop-by-Hop extension header");
        let mut hbh_pointer = &mut hbh_extension_header;
        // take the first two bytes to write in next_header and length information.
        let next_header_and_len = hbh_pointer.take_front_zero(2).unwrap();
        next_header_and_len[0] = self.prefix_builder.next_hdr;
        next_header_and_len[1] =
            u8::try_from((aligned_hbh_len - 8) / 8).expect("extension header too big");
        // After the first two bytes, we can serialize our real options.
        let options = hbh_pointer.take_rest_front_zero();
        self.hbh_options.serialize_records(options);
        // The next header in the fixed header now should be 0 (Hop-by-Hop Extension Header)
        self.prefix_builder.serialize_fixed_hdr(&mut fixed_hdr, body.len() + aligned_hbh_len, 0);
    }
}

#[cfg(test)]
mod tests {
    use std::ops::Deref;

    use byteorder::{ByteOrder, NetworkEndian};
    use packet::{Buf, InnerPacketBuilder, ParseBuffer, Serializer};

    use super::ext_hdrs::*;
    use super::*;
    use crate::ip::Ipv6ExtHdrType;
    use crate::testutil::*;
    use crate::wire::ethernet::EthernetFrame;

    const DEFAULT_SRC_IP: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const DEFAULT_DST_IP: Ipv6Addr =
        Ipv6Addr::new([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    #[test]
    fn test_parse_serialize_full_tcp() {
        use crate::wire::testdata::syn_v6::*;

        let mut buf = &ETHERNET_FRAME.bytes[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
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
        use crate::wire::testdata::dns_request_v6::*;

        let mut buf = &ETHERNET_FRAME.bytes[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
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
        let mut fixed_hdr = FixedHeader::default();
        NetworkEndian::write_u32(&mut fixed_hdr.version_tc_flowlabel[..], 0x6020_0077);
        fixed_hdr.payload_len = U16::ZERO;
        fixed_hdr.next_hdr = IpProto::Tcp.into();
        fixed_hdr.hop_limit = 64;
        fixed_hdr.src_ip = DEFAULT_SRC_IP;
        fixed_hdr.dst_ip = DEFAULT_DST_IP;
        fixed_hdr
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
        assert_eq!(packet.proto(), IpProto::Tcp);
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
        assert_eq!(packet.proto(), IpProto::Tcp);
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), [1, 2, 3, 4, 5]);
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = packet.iter_extension_hdrs().collect();
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
            IpProto::NoNextHeader.into(), // Next Header
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
        assert_eq!(packet.proto(), IpProto::NoNextHeader);
        assert_eq!(packet.src_ip(), DEFAULT_SRC_IP);
        assert_eq!(packet.dst_ip(), DEFAULT_DST_IP);
        assert_eq!(packet.body(), [1, 2, 3, 4, 5]);
        let ext_hdrs: Vec<Ipv6ExtensionHeader> = packet.iter_extension_hdrs().collect();
        assert_eq!(ext_hdrs.len(), 1);
        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, IpProto::NoNextHeader.into());
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
                action: IpParseErrorAction::DiscardPacketSendICMPNoMulticast,
            }
        );

        // Use ICMP(v4) as next header.
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = IpProto::Icmp.into();
        assert_eq!(
            (&fixed_hdr_to_bytes(fixed_hdr)[..]).parse::<Ipv6Packet<_>>().unwrap_err(),
            IpParseError::ParameterProblem {
                src_ip: DEFAULT_SRC_IP,
                dst_ip: DEFAULT_DST_IP,
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer: u32::from(NEXT_HEADER_OFFSET),
                must_send_icmp: false,
                header_len: (),
                action: IpParseErrorAction::DiscardPacketSendICMPNoMulticast,
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
                action: IpParseErrorAction::DiscardPacketSendICMPNoMulticast,
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
                action: IpParseErrorAction::DiscardPacketSendICMPNoMulticast,
            }
        );
    }

    #[test]
    fn test_partial_parse() {
        // Can't partial parse extension headers:
        #[rustfmt::skip]
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

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
        fixed_hdr.next_hdr = Ipv6ExtHdrType::HopByHopOptions.into();
        fixed_hdr.payload_len = U16::new((buf.len() - IPV6_FIXED_HDR_LEN) as u16);
        // make HopByHop malformed:
        buf[IPV6_FIXED_HDR_LEN + 1] = 10;
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut buf = &buf[..];
        let partial = buf.parse::<Ipv6PacketRaw<_>>().unwrap();
        assert_eq!(partial.fixed_hdr.bytes(), &fixed_hdr_buf[..]);
        assert!(partial.extension_hdrs.is_incomplete());
        assert!(partial.body.is_err());
        assert!(partial.proto.is_none());
        assert!(Ipv6Packet::try_from_raw(partial).is_err());

        // Incomplete body:
        let mut buf = [
            // FixedHeader (will be replaced later)
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = IpProto::Tcp.into();
        fixed_hdr.payload_len = U16::new(10);
        let fixed_hdr_buf = fixed_hdr_to_bytes(fixed_hdr);
        buf[..IPV6_FIXED_HDR_LEN].copy_from_slice(&fixed_hdr_buf);
        let mut parsebuff = &buf[..];
        let partial = parsebuff.parse::<Ipv6PacketRaw<_>>().unwrap();
        assert_eq!(partial.fixed_hdr.bytes(), &fixed_hdr_buf[..]);
        assert_eq!(partial.extension_hdrs.as_ref().unwrap().deref().len(), 0);
        assert_eq!(
            *partial.body.as_ref().unwrap().as_ref().unwrap_incomplete(),
            &buf[IPV6_FIXED_HDR_LEN..]
        );
        assert_eq!(partial.proto.unwrap(), IpProto::Tcp);
        assert!(Ipv6Packet::try_from_raw(partial).is_err());
    }

    // Return a stock Ipv6PacketBuilder with reasonable default values.
    fn new_builder() -> Ipv6PacketBuilder {
        Ipv6PacketBuilder::new(DEFAULT_SRC_IP, DEFAULT_DST_IP, 64, IpProto::Tcp)
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
        Buf::new(&mut buf_0[..], IPV6_FIXED_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
        let mut buf_1 = [0xFF; IPV6_FIXED_HDR_LEN];
        Buf::new(&mut buf_1[..], IPV6_FIXED_HDR_LEN..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    #[should_panic(expected = "Mtu, Nested { inner: Buf { buf:")]
    fn test_serialize_panic_packet_length() {
        // Test that a packet whose payload is longer than 2^16 - 1 bytes is
        // rejected.
        Buf::new(&mut [0; 1 << 16][..], ..)
            .encapsulate(new_builder())
            .serialize_vec_outer()
            .unwrap();
    }

    #[test]
    #[should_panic(expected = "packet must have at least one extension header")]
    fn test_copy_header_bytes_for_fragment_without_ext_hdrs() {
        let mut buf = &fixed_hdr_to_bytes(new_fixed_hdr())[..];
        let packet = buf.parse::<Ipv6Packet<_>>().unwrap();
        packet.copy_header_bytes_for_fragment();
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
        packet.copy_header_bytes_for_fragment();
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
        packet.copy_header_bytes_for_fragment();
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
}
