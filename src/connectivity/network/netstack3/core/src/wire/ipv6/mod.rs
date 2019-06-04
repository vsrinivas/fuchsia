// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of IPv6 packets.

pub(crate) mod ext_hdrs;

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
            0,                                  // Segments Left
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
