// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 packets.

use std::fmt::{self, Debug, Formatter};

use byteorder::{ByteOrder, NetworkEndian};
use log::debug;
use packet::{
    BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

use crate::error::{IpParseError, IpParseErrorAction, IpParseResult, ParseError};
use crate::ip::{IpProto, Ipv6, Ipv6Addr};

use crate::wire::icmp::Icmpv6ParameterProblemCode;
use crate::wire::util::records::Records;

use ext_hdrs::{
    is_valid_next_header, is_valid_next_header_upper_layer, ExtensionHeaderOptionAction,
    Ipv6ExtensionHeader, Ipv6ExtensionHeaderImpl, Ipv6ExtensionHeaderParsingContext,
    Ipv6ExtensionHeaderParsingError,
};

pub(crate) const IPV6_FIXED_HDR_LEN: usize = 40;

// Offset to the Next Header field within the fixed IPv6 header
const NEXT_HEADER_OFFSET: usize = 6;

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
            header_len,
        } => {
            let (pointer, action) = match pointer.checked_add(IPV6_FIXED_HDR_LEN as u32) {
                // Pointer calculation overflowed so set action to discard the packet and
                // 0 for the pointer (which won't be used anyways since the packet will be
                // discarded without sending an ICMP response).
                None => (0, IpParseErrorAction::DiscardPacket),
                // Pointer calculation didn't overflow so set action to send an ICMP
                // message to the source of the original packet and the pointer value
                // to what we calculated.
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendICMP),
            };

            IpParseError::ParameterProblem {
                src_ip: Ipv6Addr::new(hdr.src_ip),
                dst_ip: Ipv6Addr::new(hdr.dst_ip),
                code: Icmpv6ParameterProblemCode::ErroneousHeaderField,
                pointer,
                must_send_icmp,
                header_len: IPV6_FIXED_HDR_LEN + header_len,
                action,
            }
        }
        Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
            pointer,
            must_send_icmp,
            header_len,
        } => {
            let (pointer, action) = match pointer.checked_add(IPV6_FIXED_HDR_LEN as u32) {
                None => (0, IpParseErrorAction::DiscardPacket),
                Some(p) => (p, IpParseErrorAction::DiscardPacketSendICMP),
            };

            IpParseError::ParameterProblem {
                src_ip: Ipv6Addr::new(hdr.src_ip),
                dst_ip: Ipv6Addr::new(hdr.dst_ip),
                code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                pointer,
                must_send_icmp,
                header_len: IPV6_FIXED_HDR_LEN + header_len,
                action,
            }
        }
        Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
            pointer,
            must_send_icmp,
            header_len,
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
                src_ip: Ipv6Addr::new(hdr.src_ip),
                dst_ip: Ipv6Addr::new(hdr.dst_ip),
                code: Icmpv6ParameterProblemCode::UnrecognizedIpv6Option,
                pointer,
                must_send_icmp,
                header_len: IPV6_FIXED_HDR_LEN + header_len,
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
    payload_len: [u8; 2],
    next_hdr: u8,
    hop_limit: u8,
    src_ip: [u8; 16],
    dst_ip: [u8; 16],
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

    fn payload_len(&self) -> u16 {
        NetworkEndian::read_u16(&self.payload_len)
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

impl<B: ByteSlice> ParsablePacket<B, ()> for Ipv6Packet<B> {
    type Error = IpParseError<Ipv6>;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.fixed_hdr.bytes().len() + self.extension_hdrs.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: ()) -> IpParseResult<Ipv6, Self> {
        let total_len = buffer.len();
        let fixed_hdr = buffer
            .take_obj_front::<FixedHeader>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;

        if (fixed_hdr.payload_len() as usize) > buffer.len() {
            return debug_err!(
                Err(ParseError::Format.into()),
                "Payload length greater than buffer"
            );
        }

        // Make sure that the fixed header has a valid next header before parsing
        // extension headers.
        if !is_valid_next_header(fixed_hdr.next_hdr, true) {
            return debug_err!(
                Err(IpParseError::ParameterProblem {
                    src_ip: Ipv6Addr::new(fixed_hdr.src_ip),
                    dst_ip: Ipv6Addr::new(fixed_hdr.dst_ip),
                    code: Icmpv6ParameterProblemCode::UnrecognizedNextHeaderType,
                    pointer: NEXT_HEADER_OFFSET as u32,
                    must_send_icmp: false,
                    header_len: IPV6_FIXED_HDR_LEN,
                    action: IpParseErrorAction::DiscardPacketSendICMP,
                }),
                "Unrecognized next header value"
            );
        }

        let mut extension_hdr_context = Ipv6ExtensionHeaderParsingContext::new(fixed_hdr.next_hdr);

        let extension_hdrs =
            Records::parse_bv_with_mut_context(&mut buffer, &mut extension_hdr_context)
                .map_err(|e| ext_hdr_err_fn(&fixed_hdr, e))?;

        // Make sure the last extension header's Next Header points to a valid upper layer protocol
        // Note, we can't just to convert the Next Header value to an `IpProto` type and check to
        // see if we end up with an `IpProto::Other` value because not all the possible values in
        // `IpProto` are valid (such as `IpProto::Icmp`, the IPv4 ICMP). If we have extension headers
        // our context's (`extension_hdr_context`) `next_header` would be updated with the last extension
        // header's Next Header value. This will also work if we don't have any extension
        // headers. Let's consider that scenario: When we have no extension headers, the
        // Next Header value in the fixed header will be a valid upper layer protocol value.
        // `parse_bv_with_mut_context` will return almost immediately without doing any
        // actual work when it checks the context's (`extension_hdr_context`) `next_header`,
        // value and ends parsing since according to our context, its data is for an upper layer
        // protocol. Now, since nothing was parsed, our context was never modified, so the
        // next header value it was initialized with when calling `Ipv6ExtensionHeaderParsingContext::new`,
        // will not have changed. We simply use that value and assign it to proto below.

        let proto = extension_hdr_context.next_header;
        debug_assert!(is_valid_next_header_upper_layer(proto));
        let proto = IpProto::from(proto);

        // Make sure that the amount of bytes we used for extension headers isn't greater than the
        // number of bytes specified in the fixed header's payload length.
        if extension_hdrs.bytes().len() > fixed_hdr.payload_len() as usize {
            return debug_err!(
                Err(ParseError::Format.into()),
                "extension hdrs size more than payload length"
            );
        }

        let packet = Ipv6Packet { fixed_hdr, extension_hdrs, body: buffer.into_rest(), proto };
        if packet.fixed_hdr.version() != 6 {
            return debug_err!(
                Err(ParseError::Format.into()),
                "unexpected IP version: {}",
                packet.fixed_hdr.version()
            );
        }

        // As per Section 3 of RFC 8200, payload length includes the length of
        // the extension headers as well, so we make sure the size of the body is
        // equal to the payload length after subtracting the size of the extension
        // headers. We know that the subtraction below won't underflow because we
        // check to make sure that the size of the extension headers isn't greater
        // than the payload length.
        if packet.body.len()
            != (packet.fixed_hdr.payload_len() as usize - packet.extension_hdrs.bytes().len())
        {
            return debug_err!(
                Err(ParseError::Format.into()),
                "payload length does not match header"
            );
        }

        Ok(packet)
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
        Ipv6Addr::new(self.fixed_hdr.src_ip)
    }

    /// The destination IP address.
    pub(crate) fn dst_ip(&self) -> Ipv6Addr {
        Ipv6Addr::new(self.fixed_hdr.dst_ip)
    }

    fn header_len(&self) -> usize {
        self.fixed_hdr.bytes().len() + self.extension_hdrs.bytes().len()
    }

    fn payload_len(&self) -> usize {
        self.body.len()
    }

    /// Construct a builder with the same contents as this packet.
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

/// A builder for IPv6 packets.
#[derive(Debug)]
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
    pub(crate) fn new(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        hop_limit: u8,
        next_hdr: IpProto,
    ) -> Ipv6PacketBuilder {
        Ipv6PacketBuilder {
            ds: 0,
            ecn: 0,
            flowlabel: 0,
            hop_limit,
            next_hdr: next_hdr.into(),
            src_ip,
            dst_ip,
        }
    }

    /// Set the Differentiated Services (DS).
    ///
    /// # Panics
    ///
    /// `ds` panics if `ds` is greater than 2^6 - 1.
    pub(crate) fn ds(&mut self, ds: u8) {
        assert!(ds <= 1 << 6, "invalid DS: {}", ds);
        self.ds = ds;
    }

    /// Set the Explicit Congestion Notification (ECN).
    ///
    /// # Panics
    ///
    /// `ecn` panics if `ecn` is greater than 3.
    pub(crate) fn ecn(&mut self, ecn: u8) {
        assert!(ecn <= 0b11, "invalid ECN: {}", ecn);
        self.ecn = ecn
    }

    /// Set the flowlabel.
    ///
    /// # Panics
    ///
    /// `flowlabel` panics if `flowlabel` is greater than 2^20 - 1.
    pub(crate) fn flowlabel(&mut self, flowlabel: u32) {
        assert!(flowlabel <= 1 << 20, "invalid flowlabel: {:x}", flowlabel);
        self.flowlabel = flowlabel;
    }
}

impl PacketBuilder for Ipv6PacketBuilder {
    fn header_len(&self) -> usize {
        // TODO(joshlf): Update when we support serializing extension headers
        IPV6_FIXED_HDR_LEN
    }

    fn min_body_len(&self) -> usize {
        0
    }

    fn max_body_len(&self) -> usize {
        (1 << 16) - 1
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize(self, mut buffer: SerializeBuffer) {
        let (mut header, body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        // TODO(tkilbourn): support extension headers
        let mut fixed_hdr =
            header.take_obj_front_zero::<FixedHeader>().expect("too few bytes for IPv6 header");

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
        debug_assert!(body.len() <= std::u16::MAX as usize);
        let payload_len = body.len() as u16;
        NetworkEndian::write_u16(&mut fixed_hdr.payload_len, payload_len);
        fixed_hdr.next_hdr = self.next_hdr;
        fixed_hdr.hop_limit = self.hop_limit;
        fixed_hdr.src_ip = self.src_ip.ipv6_bytes();
        fixed_hdr.dst_ip = self.dst_ip.ipv6_bytes();
    }
}

pub(crate) mod ext_hdrs {
    use std::convert::TryFrom;
    use std::marker::PhantomData;

    use byteorder::{ByteOrder, NetworkEndian};
    use packet::BufferView;
    use zerocopy::LayoutVerified;

    use crate::ip::{IpProto, Ipv6Addr, Ipv6ExtHdrType};
    use crate::wire::util::records::{
        LimitedRecords, LimitedRecordsImpl, LimitedRecordsImplLayout, Records, RecordsContext,
        RecordsImpl, RecordsImplLayout,
    };

    /// An IPv6 Extension Header.
    #[derive(Debug)]
    pub(crate) struct Ipv6ExtensionHeader<'a> {
        // Marked as `pub(super)` because it is only used in tests within
        // the `crate::wire::ipv6` (`super`) module.
        pub(super) next_header: u8,
        data: Ipv6ExtensionHeaderData<'a>,
    }

    impl<'a> Ipv6ExtensionHeader<'a> {
        pub(crate) fn data(&self) -> &Ipv6ExtensionHeaderData<'a> {
            &self.data
        }
    }

    /// The data associated with an IPv6 Extension Header.
    #[derive(Debug)]
    pub(crate) enum Ipv6ExtensionHeaderData<'a> {
        HopByHopOptions { options: Records<&'a [u8], HopByHopOptionsImpl> },
        Routing { routing_data: RoutingData<'a> },
        Fragment { fragment_data: FragmentData<'a> },
        DestinationOptions { options: Records<&'a [u8], DestinationOptionsImpl> },
    }

    //
    // Records parsing for IPv6 Extension Header
    //

    /// Possible errors that can happen when parsing IPv6 Extension Headers.
    #[derive(Debug)]
    pub(crate) enum Ipv6ExtensionHeaderParsingError {
        // `pointer` is the offset from the beginning of the first extension header
        // to the point of error. `must_send_icmp` is a flag that requires us to send
        // an ICMP response if true. `header_len` is the size of extension headers before
        // encountering an error (number of bytes from successfully parsed
        // extension headers).
        ErroneousHeaderField {
            pointer: u32,
            must_send_icmp: bool,
            header_len: usize,
        },
        UnrecognizedNextHeader {
            pointer: u32,
            must_send_icmp: bool,
            header_len: usize,
        },
        UnrecognizedOption {
            pointer: u32,
            must_send_icmp: bool,
            header_len: usize,
            action: ExtensionHeaderOptionAction,
        },
        BufferExhausted,
        MalformedData,
    }

    /// Context that gets passed around when parsing IPv6 Extension Headers.
    #[derive(Debug, Clone)]
    pub(crate) struct Ipv6ExtensionHeaderParsingContext {
        // Next expected header.
        // Marked as `pub(super)` because it is inly used in tests within
        // the `crate::wire::ipv6` (`super`) module.
        pub(super) next_header: u8,

        // Whether context is being used for iteration or not.
        iter: bool,

        // Counter for number of extension headers parsed.
        headers_parsed: usize,

        // Byte count of successfully parsed extension headers.
        bytes_parsed: usize,
    }

    impl Ipv6ExtensionHeaderParsingContext {
        pub(crate) fn new(next_header: u8) -> Ipv6ExtensionHeaderParsingContext {
            Ipv6ExtensionHeaderParsingContext {
                iter: false,
                headers_parsed: 0,
                next_header,
                bytes_parsed: 0,
            }
        }
    }

    impl RecordsContext for Ipv6ExtensionHeaderParsingContext {
        fn clone_for_iter(&self) -> Self {
            let mut ret = self.clone();
            ret.iter = true;
            ret
        }
    }

    /// Implement the actual parsing of IPv6 Extension Headers.
    #[derive(Debug)]
    pub(crate) struct Ipv6ExtensionHeaderImpl;

    impl Ipv6ExtensionHeaderImpl {
        /// Make sure a Next Header value in an extension header is valid.
        fn valid_next_header(next_header: u8) -> bool {
            // Passing false to `is_valid_next_header`'s `for_fixed_header` parameter because
            // this function will never be called when checking the Next Header field
            // of the fixed header (which would be the first Next Header).
            is_valid_next_header(next_header, false)
        }

        /// Get the first two bytes if possible and return them.
        ///
        /// `get_next_hdr_and_len` takes the first two bytes from `data` and
        /// treats them as the Next Header and Hdr Ext Len fields. With the
        /// Next Header field, `get_next_hdr_and_len` makes sure it is a valid
        /// value before returning the Next Header and Hdr Ext Len fields.
        fn get_next_hdr_and_len<'a, BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &Ipv6ExtensionHeaderParsingContext,
        ) -> Result<(u8, u8), Ipv6ExtensionHeaderParsingError> {
            let next_header = data
                .take_front(1)
                .map(|x| x[0])
                .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

            // Make sure we recognize the next header.
            // When parsing headers, if we encounter a next header value we don't
            // recognize, we SHOULD send back an ICMP response. Since we only SHOULD,
            // we set `must_send_icmp` to `false`.
            if !Self::valid_next_header(next_header) {
                return Err(Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                    pointer: context.bytes_parsed as u32,
                    must_send_icmp: false,
                    header_len: context.bytes_parsed,
                });
            }

            let hdr_ext_len = data
                .take_front(1)
                .map(|x| x[0])
                .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

            Ok((next_header, hdr_ext_len))
        }

        /// Parse Hop By Hop Options Extension Header.
        // TODO(ghanan): Look into implementing the IPv6 Jumbo Payload option
        //               (https://tools.ietf.org/html/rfc2675) and the router
        //               alert option (https://tools.ietf.org/html/rfc2711).
        fn parse_hop_by_hop_options<'a, BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Ipv6ExtensionHeaderParsingContext,
        ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError>
        {
            let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

            // As per RFC 8200 section 4.3, Hdr Ext Len is the length of this extension
            // header in  8-octect units, not including the first 8 octets (where 2 of
            // them are the Next Header and the Hdr Ext Len fields). Since we already
            // 'took' the Next Header and Hdr Ext Len octets, we need to make sure
            // we have (Hdr Ext Len) * 8 + 6 bytes bytes in `data`.
            let expected_len = (hdr_ext_len as usize) * 8 + 6;

            let options = data
                .take_front(expected_len)
                .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

            let options_context = ExtensionHeaderOptionContext::new();
            let options = Records::parse_with_context(options, options_context).map_err(|e| {
                // We know the below `try_from` call will not result in a `None` value because
                // the maximum size of an IPv6 packet's payload (extension headers + body) is
                // `std::u32::MAX`. This maximum size is only possible when using IPv6
                // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
                // length. If we receive such a hypothetical packet with the maximum possible
                // payload length which only contains extension headers, we know that the offset
                // of any location within the payload must fit within an `u32`. If the packet is
                // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
                // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
                // is significantly less than the maximum possible size of a jumbogram.
                ext_hdr_opt_err_to_ext_hdr_err(
                    u32::try_from(context.bytes_parsed + 2).unwrap(),
                    context.bytes_parsed,
                    e,
                )
            })?;

            // Update context
            context.next_header = next_header;
            context.headers_parsed += 1;
            context.bytes_parsed += 2 + expected_len;

            return Ok(Some(Some(Ipv6ExtensionHeader {
                next_header,
                data: Ipv6ExtensionHeaderData::HopByHopOptions { options },
            })));
        }

        /// Parse Routing Extension Header.
        fn parse_routing<'a, BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Ipv6ExtensionHeaderParsingContext,
        ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError>
        {
            // All routing extension headers (regardless of type) will have
            // 4 bytes worth of data we need to look at.
            let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;
            let routing_data = data
                .take_front(2)
                .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;;
            let routing_type = routing_data[0];
            let segments_left = routing_data[1];

            // RFC2460 section 4.4 only defines a routing type of 0
            if routing_type != 0 {
                // If we receive a routing header with a non 0 router type,
                // what we do depends on the segments left. If segments left is
                // 0, we must ignore the routing header and continue processing
                // other headers. If segments left is not 0, we need to discard
                // this packet and send an ICMP Parameter Problem, Code 0 with a
                // pointer to this unrecognized routing type.
                if segments_left == 0 {
                    return Ok(Some(None));
                } else {
                    // As per RFC 8200, if we encounter a routing header with an unrecognized
                    // routing type, and segments left is non-zero, we MUST discard the packet
                    // and send and ICMP Parameter Problem response.
                    return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                        pointer: (context.bytes_parsed as u32) + 2,
                        must_send_icmp: true,
                        header_len: context.bytes_parsed,
                    });
                }
            }

            // Parse Routing Type 0 specific data

            // Each address takes up 16 bytes.
            // As per RFC 8200 section 4.4, Hdr Ext Len is the length of this extension
            // header in  8-octet units, not including the first 8 octets.
            //
            // Given this information, we know that to find the number of addresses,
            // we can simply divide the HdrExtLen by 2 to get the number of addresses.

            // First check to make sure we have enough data. Note, routing type 0 headers
            // have a 4 byte reserved field immediately after the first 4 bytes (before
            // the first address) so we account for that when we do our check as well.
            let expected_len = (hdr_ext_len as usize) * 8 + 4;

            if expected_len > data.len() {
                return Err(Ipv6ExtensionHeaderParsingError::BufferExhausted);
            }

            // If HdrExtLen is an odd number, send an ICMP Parameter Problem, code 0,
            // pointing to the HdrExtLen field.
            if (hdr_ext_len & 0x1) == 0x1 {
                return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                    pointer: (context.bytes_parsed as u32) + 1,
                    must_send_icmp: true,
                    header_len: context.bytes_parsed,
                });
            }

            // Discard 4 reserved bytes, but because of our earlier check to make sure
            // `data` contains at least `expected_len` bytes, we assert that we actually
            // get some bytes back.
            assert!(data.take_front(4).is_some());

            let num_addresses = (hdr_ext_len as usize) / 2;

            if (segments_left as usize) > num_addresses {
                // Segments Left cannot be greater than the number of addresses.
                // If it is, we need to send an ICMP Parameter Problem, Code 0,
                // pointing to the Segments Left field.
                return Err(Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                    pointer: (context.bytes_parsed as u32) + 3,
                    must_send_icmp: true,
                    header_len: context.bytes_parsed,
                });
            }

            // Each address is an IPv6 Address which requires 16 bytes.
            // The below call to `take_front` is guaranteed to succeed because
            // we have already cheked to make sure we have enough bytes
            // in `data` to handle the total number of addresses, `num_addresses`.
            let addresses = data.take_front(num_addresses * 16).unwrap();

            // This is also guranteed to succeed because of the same comments as above.
            let addresses = LimitedRecords::parse_with_context(addresses, num_addresses).unwrap();

            // Update context
            context.next_header = next_header;
            context.headers_parsed += 1;
            context.bytes_parsed += 4 + expected_len;

            return Ok(Some(Some(Ipv6ExtensionHeader {
                next_header,
                data: Ipv6ExtensionHeaderData::Routing {
                    routing_data: RoutingData {
                        bytes: routing_data,
                        type_specific_data: RoutingTypeSpecificData::RoutingType0 { addresses },
                    },
                },
            })));
        }

        /// Parse Fragment Extension Header.
        fn parse_fragment<'a, BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Ipv6ExtensionHeaderParsingContext,
        ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError>
        {
            // Fragment Extension Header requires exactly 8 bytes so make sure
            // `data` has at least 8 bytes left. If `data` has at least 8 bytes left,
            // we are guaranteed that all `take_front` calls done by this
            // method will succeed since we will never attempt to call `take_front`
            // with more than 8 bytes total.
            if data.len() < 8 {
                return Err(Ipv6ExtensionHeaderParsingError::BufferExhausted);
            }

            // For Fragment headers, we do not actually have a HdrExtLen field. Instead,
            // the second byte in the header (where HdrExtLen would normally exist), is
            // a reserved field, so we can simply ignore it for now.
            let (next_header, _) = Self::get_next_hdr_and_len(data, context)?;

            // Update context
            context.next_header = next_header;
            context.headers_parsed += 1;
            context.bytes_parsed += 8;

            return Ok(Some(Some(Ipv6ExtensionHeader {
                next_header,
                data: Ipv6ExtensionHeaderData::Fragment {
                    fragment_data: FragmentData { bytes: data.take_front(6).unwrap() },
                },
            })));
        }

        /// Parse Destination Options Extension Header.
        fn parse_destination_options<'a, BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Ipv6ExtensionHeaderParsingContext,
        ) -> Result<Option<Option<Ipv6ExtensionHeader<'a>>>, Ipv6ExtensionHeaderParsingError>
        {
            let (next_header, hdr_ext_len) = Self::get_next_hdr_and_len(data, context)?;

            // As per RFC 8200 section 4.6, Hdr Ext Len is the length of this extension
            // header in  8-octet units, not including the first 8 octets (where 2 of
            // them are the Next Header and the Hdr Ext Len fields).
            let expected_len = (hdr_ext_len as usize) * 8 + 6;

            let options = data
                .take_front(expected_len)
                .ok_or_else(|| Ipv6ExtensionHeaderParsingError::BufferExhausted)?;

            let options_context = ExtensionHeaderOptionContext::new();
            let options = Records::parse_with_context(options, options_context).map_err(|e| {
                // We know the below `try_from` call will not result in a `None` value because
                // the maximum size of an IPv6 packet's payload (extension headers + body) is
                // `std::u32::MAX`. This maximum size is only possible when using IPv6
                // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
                // length. If we receive such a hypothetical packet with the maximum possible
                // payload length which only contains extension headers, we know that the offset
                // of any location within the payload must fit within an `u32`. If the packet is
                // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
                // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
                // is significantly less than the maximum possible size of a jumbogram.
                ext_hdr_opt_err_to_ext_hdr_err(
                    u32::try_from(context.bytes_parsed + 2).unwrap(),
                    context.bytes_parsed,
                    e,
                )
            })?;

            // Update context
            context.next_header = next_header;
            context.headers_parsed += 1;
            context.bytes_parsed += 2 + expected_len;

            return Ok(Some(Some(Ipv6ExtensionHeader {
                next_header,
                data: Ipv6ExtensionHeaderData::DestinationOptions { options },
            })));
        }
    }

    impl RecordsImplLayout for Ipv6ExtensionHeaderImpl {
        type Context = Ipv6ExtensionHeaderParsingContext;
        type Error = Ipv6ExtensionHeaderParsingError;
    }

    impl<'a> RecordsImpl<'a> for Ipv6ExtensionHeaderImpl {
        type Record = Ipv6ExtensionHeader<'a>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            let expected_hdr = context.next_header;

            match Ipv6ExtHdrType::from(expected_hdr) {
                Ipv6ExtHdrType::HopByHopOptions => Self::parse_hop_by_hop_options(data, context),
                Ipv6ExtHdrType::Routing => Self::parse_routing(data, context),
                Ipv6ExtHdrType::Fragment => Self::parse_fragment(data, context),
                Ipv6ExtHdrType::DestinationOptions => {
                    Self::parse_destination_options(data, context)
                }

                _ | Ipv6ExtHdrType::Other(_) => {
                    if is_valid_next_header_upper_layer(expected_hdr) {
                        // Stop parsing extension headers when we find a Next Header value
                        // for a higher level protocol.
                        Ok(None)
                    } else {
                        // Should never end up here because we guarantee that if we hit an
                        // invalid Next Header field while parsing extension headers, we will
                        // return an error when we see it right away. Since the only other time
                        // `context.next_header` can get an invalid value assigned is when we parse
                        // the fixed IPv6 header, but we check if the next header is valid before
                        // parsing extension headers.

                        unreachable!("Should never try parsing an extension header with an unrecognized type");
                    }
                }
            }
        }
    }

    //
    // Hop-By-Hop Options
    //

    type HopByHopOption<'a> = ExtensionHeaderOption<HopByHopOptionData<'a>>;
    type HopByHopOptionsImpl = ExtensionHeaderOptionImpl<HopByHopOptionDataImpl>;

    /// HopByHop Options Extension header data.
    #[derive(Debug)]
    pub(crate) enum HopByHopOptionData<'a> {
        Unrecognized { kind: u8, len: u8, data: &'a [u8] },
    }

    /// Impl for Hop By Hop Options parsing.
    #[derive(Debug)]
    pub(crate) struct HopByHopOptionDataImpl;

    impl ExtensionHeaderOptionDataImplLayout for HopByHopOptionDataImpl {
        type Context = ();
    }

    impl<'a> ExtensionHeaderOptionDataImpl<'a> for HopByHopOptionDataImpl {
        type OptionData = HopByHopOptionData<'a>;

        fn parse_option(
            kind: u8,
            data: &'a [u8],
            context: &mut Self::Context,
            allow_unrecognized: bool,
        ) -> Option<Self::OptionData> {
            if allow_unrecognized {
                Some(HopByHopOptionData::Unrecognized { kind, len: data.len() as u8, data })
            } else {
                None
            }
        }
    }

    //
    // Routing
    //

    /// Routing Extension header data.
    #[derive(Debug)]
    pub(crate) struct RoutingData<'a> {
        bytes: &'a [u8],
        type_specific_data: RoutingTypeSpecificData<'a>,
    }

    impl<'a> RoutingData<'a> {
        pub(crate) fn routing_type(&self) -> u8 {
            debug_assert!(self.bytes.len() >= 2);
            self.bytes[0]
        }

        pub(crate) fn segments_left(&self) -> u8 {
            debug_assert!(self.bytes.len() >= 2);
            self.bytes[1]
        }

        pub(crate) fn type_specific_data(&self) -> &RoutingTypeSpecificData<'a> {
            &self.type_specific_data
        }
    }

    /// Routing Type specific data.
    #[derive(Debug)]
    pub(crate) enum RoutingTypeSpecificData<'a> {
        RoutingType0 { addresses: LimitedRecords<&'a [u8], RoutingType0Impl> },
    }

    #[derive(Debug)]
    pub(crate) struct RoutingType0Impl;

    impl LimitedRecordsImplLayout for RoutingType0Impl {
        type Error = ();
        const EXACT_LIMIT_ERROR: Option<()> = Some(());
    }

    impl<'a> LimitedRecordsImpl<'a> for RoutingType0Impl {
        type Record = LayoutVerified<&'a [u8], Ipv6Addr>;

        fn parse<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            match data.take_obj_front() {
                None => Err(()),
                Some(i) => Ok(Some(Some(i))),
            }
        }
    }

    //
    // Fragment
    //

    /// Fragment Extension header data.
    ///
    /// As per RFC 8200, section 4.5 the fragment header is structured as:
    /// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    /// |  Next Header  |   Reserved    |      Fragment Offset    |Res|M|
    /// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    /// |                         Identification                        |
    /// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    ///
    /// where Fragment Offset is 13 bits, Res is a reserved 2 bits and M
    /// is a 1 bit flag. Identification is a 32bit value.
    #[derive(Debug)]
    pub(crate) struct FragmentData<'a> {
        bytes: &'a [u8],
    }

    impl<'a> FragmentData<'a> {
        pub(crate) fn fragment_offset(&self) -> u16 {
            debug_assert!(self.bytes.len() == 6);
            (((self.bytes[0] as u16) << 5) | ((self.bytes[1] as u16) >> 3))
        }

        pub(crate) fn m_flag(&self) -> bool {
            debug_assert!(self.bytes.len() == 6);
            ((self.bytes[1] & 0x1) == 0x01)
        }

        pub(crate) fn identification(&self) -> u32 {
            debug_assert!(self.bytes.len() == 6);
            NetworkEndian::read_u32(&self.bytes[2..6])
        }
    }

    //
    // Destination Options
    //

    type DestinationOption<'a> = ExtensionHeaderOption<DestinationOptionData<'a>>;
    type DestinationOptionsImpl = ExtensionHeaderOptionImpl<DestinationOptionDataImpl>;

    /// Destination Options extension header data.
    #[derive(Debug)]
    pub(crate) enum DestinationOptionData<'a> {
        Unrecognized { kind: u8, len: u8, data: &'a [u8] },
    }

    /// Impl for Destination Options parsing.
    #[derive(Debug)]
    pub(crate) struct DestinationOptionDataImpl;

    impl ExtensionHeaderOptionDataImplLayout for DestinationOptionDataImpl {
        type Context = ();
    }

    impl<'a> ExtensionHeaderOptionDataImpl<'a> for DestinationOptionDataImpl {
        type OptionData = DestinationOptionData<'a>;

        fn parse_option(
            kind: u8,
            data: &'a [u8],
            context: &mut Self::Context,
            allow_unrecognized: bool,
        ) -> Option<Self::OptionData> {
            if allow_unrecognized {
                Some(DestinationOptionData::Unrecognized { kind, len: data.len() as u8, data })
            } else {
                None
            }
        }
    }

    //
    // Generic Extension Header who's data are options.
    //

    /// Context that gets passed around when parsing IPv6 Extension Header options.
    #[derive(Debug, Clone)]
    pub(crate) struct ExtensionHeaderOptionContext<C: Sized + Clone> {
        // Counter for number of options parsed.
        options_parsed: usize,

        // Byte count of succesfully parsed options.
        bytes_parsed: usize,

        // Extension header specific context data.
        specific_context: C,
    }

    impl<C: Sized + Clone + Default> ExtensionHeaderOptionContext<C> {
        fn new() -> Self {
            ExtensionHeaderOptionContext {
                options_parsed: 0,
                bytes_parsed: 0,
                specific_context: C::default(),
            }
        }
    }

    impl<C: Sized + Clone> RecordsContext for ExtensionHeaderOptionContext<C> {}

    /// Basic associated types required by `ExtensionHeaderOptionDataImpl`.
    pub(crate) trait ExtensionHeaderOptionDataImplLayout {
        type Context: RecordsContext;
    }

    /// An implementation of an extension header specific option data parser.
    pub(crate) trait ExtensionHeaderOptionDataImpl<'a>:
        ExtensionHeaderOptionDataImplLayout
    {
        /// Extension header specific option data.
        ///
        /// Note, `OptionData` does not need to hold general option data as defined by
        /// RFC 8200 section 4.2. It should only hold extension header specific option
        /// data.
        type OptionData: Sized;

        /// Parse an option of a given `kind` from `data`.
        ///
        /// When `kind` is recognized returns `Ok(o)` where `o` is a successfully parsed
        /// option. When `kind` is not recognized, returnd `None` if `allow_unrecognized`
        /// is `false`. If `kind` is not recognized but `allow_unrecognized` is `true`,
        /// returns an `Ok(o)` where `o` holds option data without actually parsing it
        /// (i.e. an unrecognized type that simply keeps track of the `kind` and `data`
        /// that was passed to `parse_option`).
        fn parse_option(
            kind: u8,
            data: &'a [u8],
            context: &mut Self::Context,
            allow_unrecognized: bool,
        ) -> Option<Self::OptionData>;
    }

    /// Generic implementation of extension header options parsing.
    ///
    /// `ExtensionHeaderOptionImpl` handles the common implementation details
    /// of extension header options and lets `O` (which implements
    /// `ExtensionHeaderOptionDataImpl`) handle the extension header specific
    /// option parsing.
    #[derive(Debug)]
    pub(crate) struct ExtensionHeaderOptionImpl<O>(PhantomData<O>);

    impl<O> ExtensionHeaderOptionImpl<O> {
        const PAD1: u8 = 0;
        const PADN: u8 = 1;
    }

    impl<O> RecordsImplLayout for ExtensionHeaderOptionImpl<O>
    where
        O: ExtensionHeaderOptionDataImplLayout,
    {
        type Error = ExtensionHeaderOptionParsingError;
        type Context = ExtensionHeaderOptionContext<O::Context>;
    }

    impl<'a, O> RecordsImpl<'a> for ExtensionHeaderOptionImpl<O>
    where
        O: ExtensionHeaderOptionDataImpl<'a>,
    {
        type Record = ExtensionHeaderOption<O::OptionData>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            // If we have no more bytes left, we are done.
            let kind = match data.take_front(1).map(|x| x[0]) {
                None => return Ok(None),
                Some(k) => k,
            };

            // Will never get an error because we only use the 2 least significant bits which
            // can only have a max value of 3 and all values in [0, 3] are valid values of
            // `ExtensionHeaderOptionAction`.
            let action =
                ExtensionHeaderOptionAction::try_from((kind >> 6) & 0x3).expect("Unexpected error");
            let mutable = ((kind >> 5) & 0x1) == 0x1;
            let kind = kind & 0x1F;

            // If our kind is a PAD1, consider it a NOP.
            if kind == Self::PAD1 {
                // Update context.
                context.options_parsed += 1;
                context.bytes_parsed += 1;

                return Ok(Some(None));
            }

            let len = data
                .take_front(1)
                .map(|x| x[0])
                .ok_or_else(|| ExtensionHeaderOptionParsingError::BufferExhausted)?;

            let data = data
                .take_front(len as usize)
                .ok_or_else(|| ExtensionHeaderOptionParsingError::BufferExhausted)?;

            // If our kind is a PADN, consider it a NOP as well.
            if kind == Self::PADN {
                // Update context.
                context.options_parsed += 1;
                context.bytes_parsed += 2 + (len as usize);

                return Ok(Some(None));
            }

            // Parse the actual option data.
            match O::parse_option(
                kind,
                data,
                &mut context.specific_context,
                action == ExtensionHeaderOptionAction::SkipAndContinue,
            ) {
                Some(o) => {
                    // Update context.
                    context.options_parsed += 1;
                    context.bytes_parsed += 2 + (len as usize);

                    Ok(Some(Some(ExtensionHeaderOption { action, mutable, data: o })))
                }
                None => {
                    // Unrecognized option type.
                    match action {
                        // `O::parse_option` should never return `None` when the action is
                        // `ExtensionHeaderOptionAction::SkipAndContinue` because we expect
                        // `O::parse_option` to return something that holds the option data
                        // without actually parsing it since we pass `true` for its
                        // `allow_unrecognized` parameter.
                        ExtensionHeaderOptionAction::SkipAndContinue => unreachable!(
                            "Should never end up here since action was set to skip and continue"
                        ),
                        // We know the below `try_from` call will not result in a `None` value because
                        // the maximum size of an IPv6 packet's payload (extension headers + body) is
                        // `std::u32::MAX`. This maximum size is only possible when using IPv6
                        // jumbograms as defined by RFC 2675, which uses a 32 bit field for the payload
                        // length. If we receive such a hypothetical packet with the maximum possible
                        // payload length which only contains extension headers, we know that the offset
                        // of any location within the payload must fit within an `u32`. If the packet is
                        // a normal IPv6 packet (not a jumbogram), the maximum size of the payload is
                        // `std::u16::MAX` (as the normal payload length field is only 16 bits), which
                        // is significantly less than the maximum possible size of a jumbogram.
                        _ => Err(ExtensionHeaderOptionParsingError::UnrecognizedOption {
                            pointer: u32::try_from(context.bytes_parsed).unwrap(),
                            action,
                        }),
                    }
                }
            }
        }
    }

    /// Possible errors when parsing extension header options.
    #[derive(Debug, PartialEq, Eq)]
    pub(crate) enum ExtensionHeaderOptionParsingError {
        UnrecognizedOption { pointer: u32, action: ExtensionHeaderOptionAction },
        BufferExhausted,
    }

    /// Action to take when an unrecognized option type is encountered.
    ///
    /// `ExtensionHeaderOptionAction` is an action that MUST be taken (according
    /// to RFC 8200 section 4.2) when an an IPv6 processing node does not
    /// recognize an option's type.
    #[derive(Debug, PartialEq, Eq)]
    pub(crate) enum ExtensionHeaderOptionAction {
        /// Skip over the option and continue processing the header.
        /// value = 0.
        SkipAndContinue,

        /// Just discard the packet.
        /// value = 1.
        DiscardPacket,

        /// Discard the packet and, regardless of whether or not the packet's
        /// destination address was a multicast address, send an ICMP parameter
        /// problem, code 2 (unrecognized option), message to the packet's source
        /// address, pointing to the unrecognized type.
        /// value = 2.
        DiscardPacketSendICMP,

        /// Discard the packet and, and only if the packet's destination address
        /// was not a multicast address, send an ICMP parameter problem, code 2
        /// (unrecognized option), message to the packet's source address, pointing
        /// to the unrecognized type.
        /// value = 3.
        DiscardPacketSendICMPNoMulticast,
    }

    impl TryFrom<u8> for ExtensionHeaderOptionAction {
        type Error = ();

        fn try_from(value: u8) -> Result<Self, ()> {
            match value {
                0 => Ok(ExtensionHeaderOptionAction::SkipAndContinue),
                1 => Ok(ExtensionHeaderOptionAction::DiscardPacket),
                2 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendICMP),
                3 => Ok(ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast),
                _ => Err(()),
            }
        }
    }

    impl Into<u8> for ExtensionHeaderOptionAction {
        fn into(self) -> u8 {
            match self {
                ExtensionHeaderOptionAction::SkipAndContinue => 0,
                ExtensionHeaderOptionAction::DiscardPacket => 1,
                ExtensionHeaderOptionAction::DiscardPacketSendICMP => 2,
                ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast => 3,
            }
        }
    }

    /// Extension header option.
    ///
    /// Generic Extension header option type that has extension header specific
    /// option data (`data`) defined by an `O`. The common option format is defined in
    /// section 4.2 of RFC 8200, outlining actions and mutability for option types.
    pub(crate) struct ExtensionHeaderOption<O> {
        /// Action to take if the option type is unrecognized.
        pub(crate) action: ExtensionHeaderOptionAction,

        /// Whether or not the option data of the option can change en route to the
        /// packet's final destination. When an Authentication header is present in
        /// the packet, the option data must be treated as 0s when computing or
        /// verifying the packet's authenticating value when the option data can change
        /// en route.
        pub(crate) mutable: bool,

        /// Option data associated with a specific extension header.
        pub(crate) data: O,
    }

    //
    // Helper functions
    //

    /// Make sure a Next Header is valid.
    ///
    /// Check if the provided `next_header` is a valid Next Header value. Note,
    /// we are intentionally not allowing HopByHopOptions after the first Next
    /// Header as per section 4.1 of RFC 8200 which restricts the HopByHop extension
    /// header to only appear as the very first extension header. `is_valid_next_header`.
    /// If a caller specifies `for_fixed_header` as true, then it is assumed `next_header` is
    /// the Next Header value in the fixed header, where a HopbyHopOptions extension
    /// header number is allowed.
    pub(super) fn is_valid_next_header(next_header: u8, for_fixed_header: bool) -> bool {
        // Make sure the Next Header in the fixed header is a valid extension
        // header or a valid upper layer protocol.

        match Ipv6ExtHdrType::from(next_header) {
            // HopByHop Options Extension header as a next header value
            // is only valid if it is in the fixed header.
            Ipv6ExtHdrType::HopByHopOptions => for_fixed_header,

            // Not an IPv6 Extension header number, so make sure it is
            // a valid upper layer protocol.
            Ipv6ExtHdrType::Other(next_header) => is_valid_next_header_upper_layer(next_header),

            // All valid Extension Header numbers
            _ => true,
        }
    }

    /// Make sure a Next Header is a valid upper layer protocol.
    ///
    /// Make sure a Next Header is a valid upper layer protocol in an IPv6 packet. Note,
    /// we intentionally are not allowing ICMP(v4) since we are working on IPv6 packets.
    pub(super) fn is_valid_next_header_upper_layer(next_header: u8) -> bool {
        match IpProto::from(next_header) {
            IpProto::Igmp
            | IpProto::Tcp
            | IpProto::Tcp
            | IpProto::Udp
            | IpProto::Icmpv6
            | IpProto::NoNextHeader => true,
            _ => false,
        }
    }

    /// Convert an `ExtensionHeaderOptionParsingError` to an
    /// `Ipv6ExtensionHeaderParsingError`.
    ///
    /// `offset` is the offset of the start of the options containing the error, `err`,
    /// from the end of the fixed header in an IPv6 packet. `header_len` is the
    /// length of the IPv6 header (including extension headers) that we know about up
    /// to the point of the error, `err`. Note, any data in a packet after the first
    /// `header_len` bytes is not parsed, so its context is unknown.
    fn ext_hdr_opt_err_to_ext_hdr_err(
        offset: u32,
        header_len: usize,
        err: ExtensionHeaderOptionParsingError,
    ) -> Ipv6ExtensionHeaderParsingError {
        match err {
            ExtensionHeaderOptionParsingError::UnrecognizedOption { pointer, action } => {
                Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                    pointer: offset + pointer,
                    must_send_icmp: true,
                    header_len,
                    action,
                }
            }
            ExtensionHeaderOptionParsingError::BufferExhausted => {
                Ipv6ExtensionHeaderParsingError::BufferExhausted
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::wire::util::records::Records;

        #[test]
        fn test_is_valid_next_header_upper_layer() {
            // Make sure upper layer protocols like tcp is valid
            assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));
            assert!(is_valid_next_header_upper_layer(IpProto::Tcp.into()));

            // Make sure upper layer protocol ICMP(v4) is not valid
            assert!(!is_valid_next_header_upper_layer(IpProto::Icmp.into()));
            assert!(!is_valid_next_header_upper_layer(IpProto::Icmp.into()));

            // Make sure any other value is not valid.
            // Note, if 255 becomes a valid value, we should fix this test
            assert!(!is_valid_next_header(255, true));
            assert!(!is_valid_next_header(255, false));
        }

        #[test]
        fn test_is_valid_next_header() {
            // Make sure HopByHop Options is only valid if it is in the first Next Header
            // (In the fixed header).
            assert!(is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), true));
            assert!(!is_valid_next_header(Ipv6ExtHdrType::HopByHopOptions.into(), false));

            // Make sure other extension headers (like routing) can be in any
            // Next Header
            assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), true));
            assert!(is_valid_next_header(Ipv6ExtHdrType::Routing.into(), false));

            // Make sure upper layer protocols like tcp can be in any Next Header
            assert!(is_valid_next_header(IpProto::Tcp.into(), true));
            assert!(is_valid_next_header(IpProto::Tcp.into(), false));

            // Make sure upper layer protocol ICMP(v4) cannot be in any Next Header
            assert!(!is_valid_next_header(IpProto::Icmp.into(), true));
            assert!(!is_valid_next_header(IpProto::Icmp.into(), false));

            // Make sure any other value is not valid.
            // Note, if 255 becomes a valid value, we should fix this test
            assert!(!is_valid_next_header(255, true));
            assert!(!is_valid_next_header(255, false));
        }

        #[test]
        fn test_hop_by_hop_options() {
            // Test parsing of Pad1 (marked as NOP)
            let buffer = [0; 10];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            assert_eq!(options.iter().count(), 0);
            assert_eq!(context.bytes_parsed, 10);
            assert_eq!(context.options_parsed, 10);

            // Test parsing of Pad1 w/ PadN (treated as NOP)
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                1, 0,                         // Pad2
                1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            assert_eq!(options.iter().count(), 0);
            assert_eq!(context.bytes_parsed, 13);
            assert_eq!(context.options_parsed, 3);

            // Test parsing with an unknown option type but its action is
            // skip/continue
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                63, 1, 0,                     // Unrecognized Option Type but can skip/continue
                1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            let options: Vec<HopByHopOption> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
            assert_eq!(context.bytes_parsed, 12);
            assert_eq!(context.options_parsed, 3);
        }

        #[test]
        fn test_hop_by_hop_options_err() {
            // Test parsing but missing last 2 bytes
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                1, 0,                         // Pad2
                1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we were short 2 by bytes");
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 2);

            // Test parsing with unknown option type but action set to discard
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                127, 0,                       // Unrecognized Option Type w/ action to discard
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacket,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);

            // Test parsing with unknown option type but action set to discard and
            // send ICMP.
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                191, 0,                       // Unrecognized Option Type w/ action to discard
                                              // & send icmp
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacketSendICMP,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);

            // Test parsing with unknown option type but action set to discard and
            // send ICMP if not sending to a multicast address
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                255, 0,                       // Unrecognized Option Type w/ action to discard
                                              // & send icmp if no multicast
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, HopByHopOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);
        }

        #[test]
        fn test_routing_type0_limited_records() {
            // Test empty buffer
            let buffer = [0; 0];
            let addresses =
                LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 0).unwrap();
            assert_eq!(addresses.iter().count(), 0);

            // Test single address
            let buffer = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];
            let addresses =
                LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 1).unwrap();
            let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
            assert_eq!(addresses.len(), 1);
            assert_eq!(
                addresses[0].bytes(),
                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
            );

            // Test multiple address
            #[rustfmt::skip]
            let buffer = [
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
            ];
            let addresses =
                LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 3).unwrap();
            let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
            assert_eq!(addresses.len(), 3);
            assert_eq!(
                addresses[0].bytes(),
                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
            );
            assert_eq!(
                addresses[1].bytes(),
                [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
            );
            assert_eq!(
                addresses[2].bytes(),
                [32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,]
            );
        }

        #[test]
        fn test_routing_type0_limited_records_err() {
            // Test single address with not enough bytes
            let buffer = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 1)
                .expect_err("Parsed successfully when we were misisng 2 bytes");

            // Test multiple addresses but not enough bytes for last one
            #[rustfmt::skip]
            let buffer = [
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
            ];
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 3)
                .expect_err("Parsed successfully when we were misisng 2 bytes");

            // Test multiple addresses but limit is more than what we expected
            #[rustfmt::skip]
            let buffer = [
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47
            ];
            LimitedRecords::<_, RoutingType0Impl>::parse_with_context(&buffer[..], 4)
                .expect_err("Parsed 3 addresses succesfully when exact limit was set to 4");
        }

        #[test]
        fn test_destination_options() {
            // Test parsing of Pad1 (marked as NOP)
            let buffer = [0; 10];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            assert_eq!(options.iter().count(), 0);
            assert_eq!(context.bytes_parsed, 10);
            assert_eq!(context.options_parsed, 10);

            // Test parsing of Pad1 w/ PadN (treated as NOP)
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                1, 0,                         // Pad2
                1, 8, 0, 0, 0, 0, 0, 0, 0, 0, // Pad10
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            assert_eq!(options.iter().count(), 0);
            assert_eq!(context.bytes_parsed, 13);
            assert_eq!(context.options_parsed, 3);

            // Test parsing with an unknown option type but its action is
            // skip/continue
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                63, 1, 0,                     // Unrecognized Option Type but can skip/continue
                1,  6, 0, 0, 0, 0, 0, 0,      // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            let options = Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                &buffer[..],
                &mut context,
            )
            .unwrap();
            let options: Vec<DestinationOption> = options.iter().collect();
            assert_eq!(options.len(), 1);
            assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
            assert_eq!(context.bytes_parsed, 12);
            assert_eq!(context.options_parsed, 3);
        }

        #[test]
        fn test_destination_options_err() {
            // Test parsing but missing last 2 bytes
            #[rustfmt::skip]
            let buffer = [
                0,                            // Pad1
                1, 0,                         // Pad2
                1, 8, 0, 0, 0, 0, 0, 0,       // Pad10 (but missing 2 bytes)
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            Records::<_, DestinationOptionsImpl>::parse_with_mut_context(&buffer[..], &mut context)
                .expect_err("Parsed successfully when we were short 2 by bytes");
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 2);

            // Test parsing with unknown option type but action set to discard
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                127, 0,                       // Unrecognized Option Type w/ action to discard
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacket,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);

            // Test parsing with unknown option type but action set to discard and
            // send ICMP.
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                191, 0,                       // Unrecognized Option Type w/ action to discard
                                              // & send icmp
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacketSendICMP,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);

            // Test parsing with unknown option type but action set to discard and
            // send ICMP if not sending to a multicast address
            #[rustfmt::skip]
            let buffer = [
                1,   1, 0,                    // Pad3
                255, 0,                       // Unrecognized Option Type w/ action to discard
                                              // & send icmp if no multicast
                1,   6, 0, 0, 0, 0, 0, 0,     // Pad8
            ];
            let mut context = ExtensionHeaderOptionContext::new();
            assert_eq!(
                Records::<_, DestinationOptionsImpl>::parse_with_mut_context(
                    &buffer[..],
                    &mut context
                )
                .expect_err("Parsed successfully when we had an unrecognized option type"),
                ExtensionHeaderOptionParsingError::UnrecognizedOption {
                    pointer: 3,
                    action: ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast,
                }
            );
            assert_eq!(context.bytes_parsed, 3);
            assert_eq!(context.options_parsed, 1);
        }

        #[test]
        fn test_hop_by_hop_options_ext_hdr() {
            // Test parsing of just a single Hop By Hop Extension Header.
            // The hop by hop options will only be pad options.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),     // Next Header
                1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,  4, 0, 0, 0, 0,       // Pad6
                63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
            ];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();
            let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
            assert_eq!(ext_hdrs.len(), 1);
            assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
            if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
                // Everything should have been a NOP/ignore except for the unrecognized type
                let options: Vec<HopByHopOption> = options.iter().collect();
                assert_eq!(options.len(), 1);
                assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
            } else {
                panic!("Should have matched HopByHopOptions {:?}", ext_hdrs[0].data());
            }
        }

        #[test]
        fn test_hop_by_hop_options_ext_hdr_err() {
            // Test parsing of just a single Hop By Hop Extension Header with errors.

            // Test with invalid Next Header
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                255,                  // Next Header (Invalid)
                0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1, 4, 0, 0, 0, 0,     // Pad6
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when the next header was invalid");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 0);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }

            // Test with invalid option type w/ action = discard.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }

            // Test with invalid option type w/ action = discard & send icmp
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }

            // Test with invalid option type w/ action = discard & send icmp if not multicast
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                          // if destination address is not a multicast
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }
        }

        #[test]
        fn test_routing_ext_hdr() {
            // Test parsing of just a single Routing Extension Header.
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(), // Next Header
                4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                   // Routing Type
                1,                   // Segments Left
                0, 0, 0, 0,          // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            ];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();
            let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
            assert_eq!(ext_hdrs.len(), 1);
            assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
            if let Ipv6ExtensionHeaderData::Routing { routing_data } = ext_hdrs[0].data() {
                assert_eq!(routing_data.routing_type(), 0);
                assert_eq!(routing_data.segments_left(), 1);

                let RoutingTypeSpecificData::RoutingType0 { addresses } =
                    routing_data.type_specific_data();
                let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
                assert_eq!(addresses.len(), 2);
                assert_eq!(
                    addresses[0].bytes(),
                    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
                );
                assert_eq!(
                    addresses[1].bytes(),
                    [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
                );
            } else {
                panic!("Should have matched Routing: {:?}", ext_hdrs[0].data());
            }
        }

        #[test]
        fn test_routing_ext_hdr_err() {
            // Test parsing of just a single Routing Extension Header with errors.

            // Test Invalid Next Header
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
            #[rustfmt::skip]
            let buffer = [
                255,                 // Next Header (Invalid)
                4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                   // Routing Type
                1,                   // Segments Left
                0, 0, 0, 0,          // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when the next header was invalid");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 0);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }

            // Test Unrecognized Routing Type
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(), // Next Header
                4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                255,                 // Routing Type (Invalid)
                1,                   // Segments Left
                0, 0, 0, 0,          // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized routing type");
            if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                // Should point to the location of the routing type.
                assert_eq!(pointer, 2);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with ErroneousHeaderField: {:?}", error);
            }

            // Test more Segments Left than addresses available
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Routing.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(), // Next Header
                4,                   // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                   // Routing Type
                3,                   // Segments Left
                0, 0, 0, 0,          // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            ];
            let error = Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(
                &buffer[..],
                context,
            )
            .expect_err(
                "Parsed succesfully when segments left was greater than the number of addresses",
            );
            if let Ipv6ExtensionHeaderParsingError::ErroneousHeaderField {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                // Should point to the location of the routing type.
                assert_eq!(pointer, 3);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with ErroneousHeaderField: {:?}", error);
            }
        }

        #[test]
        fn test_fragment_ext_hdr() {
            // Test parsing of just a single Fragment Extension Header.
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
            let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
            let identification: u32 = 3266246449;
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),                   // Next Header
                0,                                     // Reserved
                (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
                (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
                // Identification
                (identification >> 24) as u8,
                ((identification >> 16) & 0xFF) as u8,
                ((identification >> 8) & 0xFF) as u8,
                (identification & 0xFF) as u8,
            ];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();
            let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
            assert_eq!(ext_hdrs.len(), 1);
            assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());

            if let Ipv6ExtensionHeaderData::Fragment { fragment_data } = ext_hdrs[0].data() {
                assert_eq!(fragment_data.fragment_offset(), 5063);
                assert_eq!(fragment_data.m_flag(), true);
                assert_eq!(fragment_data.identification(), 3266246449);
            } else {
                panic!("Should have matched Fragment: {:?}", ext_hdrs[0].data());
            }
        }

        #[test]
        fn test_fragment_ext_hdr_err() {
            // Test parsing of just a single Fragment Extension Header with errors.

            // Test invalid Next Header
            let context = Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::Fragment.into());
            let frag_offset_res_m_flag: u16 = (5063 << 3) | 1;
            let identification: u32 = 3266246449;
            #[rustfmt::skip]
            let buffer = [
                255,                                   // Next Header (Invalid)
                0,                                     // Reserved
                (frag_offset_res_m_flag >> 8) as u8,   // Fragment Offset MSB
                (frag_offset_res_m_flag & 0xFF) as u8, // Fragment Offset LS5bits w/ Res w/ M Flag
                // Identification
                (identification >> 24) as u8,
                ((identification >> 16) & 0xFF) as u8,
                ((identification >> 8) & 0xFF) as u8,
                (identification & 0xFF) as u8,
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when the next header was invalid");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 0);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }
        }

        #[test]
        fn test_no_next_header_ext_hdr() {
            // Test parsing of just a single NoNextHeader Extension Header.
            let context = Ipv6ExtensionHeaderParsingContext::new(IpProto::NoNextHeader.into());
            #[rustfmt::skip]
            let buffer = [0, 0, 0, 0,];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();
            assert_eq!(ext_hdrs.iter().count(), 0);
        }

        #[test]
        fn test_destination_options_ext_hdr() {
            // Test parsing of just a single Destination options Extension Header.
            // The destination options will only be pad options.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),     // Next Header
                1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1, 4, 0, 0, 0, 0,        // Pad6
                63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action set to skip/continue
            ];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();
            let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
            assert_eq!(ext_hdrs.len(), 1);
            assert_eq!(ext_hdrs[0].next_header, IpProto::Tcp.into());
            if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[0].data() {
                // Everything should have been a NOP/ignore except for the unrecognized type
                let options: Vec<DestinationOption> = options.iter().collect();
                assert_eq!(options.len(), 1);
                assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
            } else {
                panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[0].data());
            }
        }

        #[test]
        fn test_destination_options_ext_hdr_err() {
            // Test parsing of just a single Destination Options Extension Header with errors.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());

            // Test with invalid Next Header
            #[rustfmt::skip]
            let buffer = [
                255,                  // Next Header (Invalid)
                0,                    // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1, 4, 0, 0, 0, 0,     // Pad6
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when the next header was invalid");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 0);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }

            // Test with invalid option type w/ action = discard.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                127, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacket);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }

            // Test with invalid option type w/ action = discard & send icmp
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }

            // Test with invalid option type w/ action = discard & send icmp if not multicast
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::DestinationOptions.into());
            #[rustfmt::skip]
            let buffer = [
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                1,   4, 0, 0, 0, 0,       // Pad6
                255, 6, 0, 0, 0, 0, 0, 0, // Unrecognized option type w/ action = discard & send icmp
                                          // if destination address is not a multicast
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(must_send_icmp);
                assert_eq!(header_len, 0);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMPNoMulticast);
            } else {
                panic!("Should have matched with UnrecognizedOption: {:?}", error);
            }
        }

        #[test]
        fn test_multiple_ext_hdrs() {
            // Test parsing of multiple extension headers.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                // HopByHop Options Extension Header
                Ipv6ExtHdrType::Routing.into(), // Next Header
                0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                       // Pad1
                1, 0,                    // Pad2
                1, 1, 0,                 // Pad3

                // Routing Extension Header
                Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
                4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                                  // Routing Type
                1,                                  // Segments Left
                0, 0, 0, 0,                         // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

                // Destination Options Extension Header
                IpProto::Tcp.into(),     // Next Header
                1,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                       // Pad1
                1,  0,                   // Pad2
                1,  1, 0,                // Pad3
                63, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
            ];
            let ext_hdrs =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .unwrap();

            let ext_hdrs: Vec<Ipv6ExtensionHeader> = ext_hdrs.iter().collect();
            assert_eq!(ext_hdrs.len(), 3);

            // Check first extension header (hop-by-hop options)
            assert_eq!(ext_hdrs[0].next_header, Ipv6ExtHdrType::Routing.into());
            if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
                // Everything should have been a NOP/ignore
                assert_eq!(options.iter().count(), 0);
            } else {
                panic!("Should have matched HopByHopOptions: {:?}", ext_hdrs[0].data());
            }

            // Check the second extension header (routing options)
            assert_eq!(ext_hdrs[1].next_header, Ipv6ExtHdrType::DestinationOptions.into());
            if let Ipv6ExtensionHeaderData::Routing { routing_data } = ext_hdrs[1].data() {
                assert_eq!(routing_data.routing_type(), 0);
                assert_eq!(routing_data.segments_left(), 1);

                let RoutingTypeSpecificData::RoutingType0 { addresses } =
                    routing_data.type_specific_data();
                let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
                assert_eq!(addresses.len(), 2);
                assert_eq!(
                    addresses[0].bytes(),
                    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
                );
                assert_eq!(
                    addresses[1].bytes(),
                    [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
                );
            } else {
                panic!("Should have matched Routing: {:?}", ext_hdrs[1].data());
            }

            // Check the third extension header (destination options)
            assert_eq!(ext_hdrs[2].next_header, IpProto::Tcp.into());
            if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[2].data() {
                // Everything should have been a NOP/ignore except for the unrecognized type
                let options: Vec<DestinationOption> = options.iter().collect();
                assert_eq!(options.len(), 1);
                assert_eq!(options[0].action, ExtensionHeaderOptionAction::SkipAndContinue);
            } else {
                panic!("Should have matched DestinationOptions: {:?}", ext_hdrs[2].data());
            }
        }

        #[test]
        fn test_multiple_ext_hdrs_errs() {
            // Test parsing of multiple extension headers with erros.

            // Test Invalid next header in the second extension header.
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                // HopByHop Options Extension Header
                Ipv6ExtHdrType::Routing.into(), // Next Header
                0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                       // Pad1
                1, 0,                    // Pad2
                1, 1, 0,                 // Pad3

                // Routing Extension Header
                255,                                // Next Header (Invalid)
                4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                                  // Routing Type
                1,                                  // Segments Left
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
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when the next header was invalid");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 8);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 8);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }

            // Test HopByHop extension header not being the very first extension header
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                // Routing Extension Header
                Ipv6ExtHdrType::HopByHopOptions.into(),    // Next Header (Valid but HopByHop restricted to first extension header)
                4,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                                  // Routing Type
                1,                                  // Segments Left
                0, 0, 0, 0,                         // Reserved
                // Addresses for Routing Header w/ Type 0
                0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

                // HopByHop Options Extension Header
                Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
                0,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                                  // Pad1
                1, 0,                               // Pad2
                1, 1, 0,                            // Pad3

                // Destination Options Extension Header
                IpProto::Tcp.into(),    // Next Header
                1,                      // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                      // Pad1
                1, 0,                   // Pad2
                1, 1, 0,                // Pad3
                1, 6, 0, 0, 0, 0, 0, 0, // Pad8
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully when a hop by hop extension header was not the fist extension header");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedNextHeader {
                pointer,
                must_send_icmp,
                header_len,
            } = error
            {
                assert_eq!(pointer, 0);
                assert!(!must_send_icmp);
                assert_eq!(header_len, 0);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }

            // Test parsing of destination options with an unrecognized option type w/ action
            // set to discard and send icmp
            let context =
                Ipv6ExtensionHeaderParsingContext::new(Ipv6ExtHdrType::HopByHopOptions.into());
            #[rustfmt::skip]
            let buffer = [
                // HopByHop Options Extension Header
                Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
                0,                       // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                       // Pad1
                1, 0,                    // Pad2
                1, 1, 0,                 // Pad3

                // Destination Options Extension Header
                IpProto::Tcp.into(),      // Next Header
                1,                        // Hdr Ext Len (In 8-octet units, not including first 8 octets)
                0,                        // Pad1
                1,   0,                   // Pad2
                1,   1, 0,                // Pad3
                191, 6, 0, 0, 0, 0, 0, 0, // Unrecognized type w/ action = discard
            ];
            let error =
                Records::<&[u8], Ipv6ExtensionHeaderImpl>::parse_with_context(&buffer[..], context)
                    .expect_err("Parsed succesfully with an unrecognized destination option type");
            if let Ipv6ExtensionHeaderParsingError::UnrecognizedOption {
                pointer,
                must_send_icmp,
                header_len,
                action,
            } = error
            {
                assert_eq!(pointer, 16);
                assert!(must_send_icmp);
                assert_eq!(header_len, 8);
                assert_eq!(action, ExtensionHeaderOptionAction::DiscardPacketSendICMP);
            } else {
                panic!("Should have matched with UnrecognizedNextHeader: {:?}", error);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ext_hdrs::*;
    use super::*;
    use crate::ip::Ipv6ExtHdrType;
    use packet::{Buf, BufferSerializer, ParseBuffer, Serializer};

    const DEFAULT_SRC_IP: Ipv6Addr =
        Ipv6Addr::new([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]);
    const DEFAULT_DST_IP: Ipv6Addr =
        Ipv6Addr::new([17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32]);

    // TODO(tkilbourn): add IPv6 versions of TCP and UDP parsing

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
        NetworkEndian::write_u16(&mut fixed_hdr.payload_len[..], 0);
        fixed_hdr.next_hdr = IpProto::Tcp.into();
        fixed_hdr.hop_limit = 64;
        fixed_hdr.src_ip = DEFAULT_SRC_IP.ipv6_bytes();
        fixed_hdr.dst_ip = DEFAULT_DST_IP.ipv6_bytes();
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
            0,                                  // Routing Type
            1,                                  // Segments Left
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
        NetworkEndian::write_u16(
            &mut fixed_hdr.payload_len[..],
            (buf.len() - IPV6_FIXED_HDR_LEN) as u16,
        );
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
        assert_eq!(ext_hdrs.len(), 3);
        // Check first extension header (hop-by-hop options)
        assert_eq!(ext_hdrs[0].next_header, Ipv6ExtHdrType::Routing.into());
        if let Ipv6ExtensionHeaderData::HopByHopOptions { options } = ext_hdrs[0].data() {
            // Everything should have been a NOP/ignore
            assert_eq!(options.iter().count(), 0);
        } else {
            panic!("Should have matched HopByHopOptions!");
        }

        // Check the second extension header (routing options)
        assert_eq!(ext_hdrs[1].next_header, Ipv6ExtHdrType::DestinationOptions.into());
        if let Ipv6ExtensionHeaderData::Routing { routing_data } = ext_hdrs[1].data() {
            assert_eq!(routing_data.routing_type(), 0);
            assert_eq!(routing_data.segments_left(), 1);

            let RoutingTypeSpecificData::RoutingType0 { addresses } =
                routing_data.type_specific_data();
            let addresses: Vec<LayoutVerified<_, Ipv6Addr>> = addresses.iter().collect();
            assert_eq!(addresses.len(), 2);
            assert_eq!(
                addresses[0].bytes(),
                [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,]
            );
            assert_eq!(
                addresses[1].bytes(),
                [16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,]
            );
        } else {
            panic!("Should have matched Routing!");
        }

        // Check the third extension header (destination options)
        assert_eq!(ext_hdrs[2].next_header, IpProto::Tcp.into());
        if let Ipv6ExtensionHeaderData::DestinationOptions { options } = ext_hdrs[2].data() {
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
        NetworkEndian::write_u16(
            &mut fixed_hdr.payload_len[..],
            (buf.len() - IPV6_FIXED_HDR_LEN) as u16,
        );
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
        NetworkEndian::write_u16(&mut fixed_hdr.payload_len[..], 2);
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
                pointer: NEXT_HEADER_OFFSET as u32,
                must_send_icmp: false,
                header_len: IPV6_FIXED_HDR_LEN,
                action: IpParseErrorAction::DiscardPacketSendICMP,
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
                pointer: NEXT_HEADER_OFFSET as u32,
                must_send_icmp: false,
                header_len: IPV6_FIXED_HDR_LEN,
                action: IpParseErrorAction::DiscardPacketSendICMP,
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
            1,                                  // Segments Left
            0, 0, 0, 0,                         // Reserved
            // Addresses for Routing Header w/ Type 0
            0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,

            // HopByHop Options Extension Header
            Ipv6ExtHdrType::DestinationOptions.into(), // Next Header
            0,                                  // Hdr Ext Len (In 8-octet units, not including first 8 octets)
            0,                                  // Pad1
            1, 0,                               // Pad2
            1, 1, 0,                            // Pad3

            // Body
            1, 2, 3, 4, 5,
        ];
        let mut fixed_hdr = new_fixed_hdr();
        fixed_hdr.next_hdr = Ipv6ExtHdrType::Routing.into();
        NetworkEndian::write_u16(
            &mut fixed_hdr.payload_len[..],
            (buf.len() - IPV6_FIXED_HDR_LEN) as u16,
        );
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
                header_len: IPV6_FIXED_HDR_LEN,
                action: IpParseErrorAction::DiscardPacketSendICMP,
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
        NetworkEndian::write_u16(
            &mut fixed_hdr.payload_len[..],
            (buf.len() - IPV6_FIXED_HDR_LEN) as u16,
        );
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
                header_len: IPV6_FIXED_HDR_LEN,
                action: IpParseErrorAction::DiscardPacketSendICMP,
            }
        );
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
        let mut buf =
            (&[0, 1, 2, 3, 4, 5, 6, 7, 8, 9]).encapsulate(builder).serialize_outer().unwrap();
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
        BufferSerializer::new_vec(Buf::new(&mut buf_0[..], IPV6_FIXED_HDR_LEN..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
        let mut buf_1 = [0xFF; IPV6_FIXED_HDR_LEN];
        BufferSerializer::new_vec(Buf::new(&mut buf_1[..], IPV6_FIXED_HDR_LEN..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_packet_length() {
        // Test that a packet whose payload is longer than 2^16 - 1 bytes is
        // rejected.
        BufferSerializer::new_vec(Buf::new(&mut [0; 1 << 16][..], ..))
            .encapsulate(new_builder())
            .serialize_outer()
            .unwrap();
    }
}
