// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing PPP packets.
//!
//! Currently supports parsing and serialization of LCP, IPCP, and IPV6CP packets and their
//! configuration options.

#![deny(missing_docs)]

pub mod ipv4;
pub mod ipv6;
pub mod link;
pub mod records;

use {
    packet::{
        BufferView, BufferViewMut, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
        SerializeBuffer,
    },
    std::convert::TryInto,
    thiserror::Error,
    zerocopy::{
        byteorder::network_endian::{U16, U32},
        AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned,
    },
};

/// The type of error that occurred while attempting to parse a packet.
#[derive(Error, Debug, PartialEq)]
pub enum ParseError {
    /// Too few bytes for header.
    #[error("Too few bytes for header.")]
    InsufficientHeaderBytes,
    /// Too few bytes for body (per header).
    #[error("Too few bytes for body (per header).")]
    InsufficientBodyBytes,
    /// Too many bytes for body (per header).
    #[error("Too many bytes for body (per header).")]
    ExcessBodyBytes,
}

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct PppHeader {
    protocol: U16,
}

impl PppHeader {
    pub fn protocol(&self) -> u16 {
        self.protocol.get()
    }
}

/// Wrapper around a parsed on-the-wire PPP header and the rest of the packet.
pub struct PppPacket<B> {
    header: LayoutVerified<B, PppHeader>,
    body: B,
}

impl<B: ByteSlice> PppPacket<B> {
    /// Extract the protocol from the wire format.
    pub fn protocol(&self) -> u16 {
        self.header.protocol()
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for PppPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let header = buffer
            .take_obj_front::<PppHeader>()
            .ok_or_else(|| ParseError::InsufficientHeaderBytes)?;
        Ok(Self { header, body: buffer.into_rest() })
    }
}

/// Builder for a PPP packet.
pub struct PppPacketBuilder {
    protocol: u16,
}

impl PppPacketBuilder {
    /// Construct with the given protocol.
    pub fn new(protocol: u16) -> Self {
        Self { protocol }
    }
}

impl PacketBuilder for PppPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(std::mem::size_of::<PppHeader>(), 0, 0, usize::max_value())
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, _, _) = buffer.parts();
        let mut header = &mut header;
        let mut header = header.take_obj_front_zero::<PppHeader>().unwrap();
        header.protocol = U16::new(self.protocol);
    }
}

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct ControlProtocolHeader {
    code: u8,
    identifier: u8,
    length: U16,
}

impl ControlProtocolHeader {
    pub fn code(&self) -> u8 {
        self.code
    }

    pub fn identifier(&self) -> u8 {
        self.identifier
    }

    pub fn length(&self) -> u16 {
        self.length.get()
    }
}

/// Wrapper around a parsed on-the-wire control protocol header and the rest of the packet.
pub struct ControlProtocolPacket<B> {
    header: LayoutVerified<B, ControlProtocolHeader>,
    body: B,
}

impl<B: ByteSlice> ControlProtocolPacket<B> {
    /// Extract the code from the wire format.
    pub fn code(&self) -> u8 {
        self.header.code()
    }

    /// Extract the identifier from the wire format.
    pub fn identifier(&self) -> u8 {
        self.header.identifier()
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for ControlProtocolPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let header = buffer
            .take_obj_front::<ControlProtocolHeader>()
            .ok_or(ParseError::InsufficientHeaderBytes)?;

        let body_length = (header.length() as usize)
            .checked_sub(header.bytes().len())
            .ok_or(ParseError::InsufficientBodyBytes)?;

        let padding = buffer.len().checked_sub(body_length).ok_or(ParseError::ExcessBodyBytes)?;

        // We did the necessary bounds check above for this to be safe. `parse`
        // is required to consume this padding from the suffix to maintain the
        // body invariant for encapsulated packets.
        buffer.take_back(padding).unwrap();

        Ok(Self { header, body: buffer.into_rest() })
    }
}

/// Builder for a control protocol packet.
pub struct ControlProtocolPacketBuilder {
    code: u8,
    identifier: u8,
}

impl ControlProtocolPacketBuilder {
    /// Construct with the given code and identifier.
    pub fn new(code: u8, identifier: u8) -> Self {
        Self { code, identifier }
    }
}

impl PacketBuilder for ControlProtocolPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            std::mem::size_of::<ControlProtocolHeader>(),
            0,
            0,
            usize::max_value(),
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, body, _) = buffer.parts();
        let mut header = &mut header;
        let mut header = header.take_obj_front_zero::<ControlProtocolHeader>().unwrap();

        let length = body
            .len()
            .try_into()
            .ok()
            .and_then(|c: u16| c.checked_add(self.constraints().header_len() as u16))
            .unwrap();

        header.code = self.code;
        header.identifier = self.identifier;
        header.length = U16::new(length);
    }
}

/// Wrapper around a parsed on-the-wire configuration packet header and the rest of the packet.
pub struct ConfigurationPacket<B> {
    body: B,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for ConfigurationPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(0, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        Ok(Self { body: buffer.into_rest() })
    }
}

/// Builder for a configuration packet.
#[derive(Default)]
pub struct ConfigurationPacketBuilder;

impl ConfigurationPacketBuilder {
    /// Construct.
    pub fn new() -> Self {
        Self {}
    }
}

impl PacketBuilder for ConfigurationPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(0, 0, 0, usize::max_value())
    }

    fn serialize(&self, _buffer: &mut SerializeBuffer<'_, '_>) {}
}

/// Wrapper around a parsed on-the-wire termination packet header and the rest of the packet.
pub struct TerminationPacket<B> {
    body: B,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for TerminationPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(0, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        Ok(Self { body: buffer.into_rest() })
    }
}

/// Builder for a termination packet.
#[derive(Default)]
pub struct TerminationPacketBuilder;

impl TerminationPacketBuilder {
    /// Construct.
    pub fn new() -> Self {
        Self {}
    }
}

impl PacketBuilder for TerminationPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(0, 0, 0, usize::max_value())
    }

    fn serialize(&self, _buffer: &mut SerializeBuffer<'_, '_>) {}
}

/// Wrapper around a parsed on-the-wire code reject packet header and the rest of the packet.
pub struct CodeRejectPacket<B> {
    body: B,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for CodeRejectPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(0, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        Ok(Self { body: buffer.into_rest() })
    }
}

/// Builder for a code reject packet.
#[derive(Default)]
pub struct CodeRejectPacketBuilder;

impl CodeRejectPacketBuilder {
    /// Construct.
    pub fn new() -> Self {
        Self {}
    }
}

impl PacketBuilder for CodeRejectPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(0, 0, 0, usize::max_value())
    }

    fn serialize(&self, _buffer: &mut SerializeBuffer<'_, '_>) {}
}

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct ProtocolRejectHeader {
    rejected_protocol: U16,
}

impl ProtocolRejectHeader {
    pub fn rejected_protocol(&self) -> u16 {
        self.rejected_protocol.get()
    }
}

/// Wrapper around a parsed on-the-wire protocol reject packet header and the rest of the packet.
pub struct ProtocolRejectPacket<B> {
    header: LayoutVerified<B, ProtocolRejectHeader>,
    body: B,
}

impl<B: ByteSlice> ProtocolRejectPacket<B> {
    /// Extract the rejected protocol from the wire format.
    pub fn rejected_protocol(&self) -> u16 {
        self.header.rejected_protocol()
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for ProtocolRejectPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let header = buffer
            .take_obj_front::<ProtocolRejectHeader>()
            .ok_or(ParseError::InsufficientHeaderBytes)?;
        Ok(Self { header, body: buffer.into_rest() })
    }
}

/// Builder for a protocol reject packet.
pub struct ProtocolRejectPacketBuilder {
    rejected_protocol: u16,
}

impl ProtocolRejectPacketBuilder {
    /// Construct with a rejected protocol.
    pub fn new(rejected_protocol: u16) -> Self {
        Self { rejected_protocol }
    }
}

impl PacketBuilder for ProtocolRejectPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            std::mem::size_of::<ProtocolRejectHeader>(),
            0,
            0,
            usize::max_value(),
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, _, _) = buffer.parts();
        let mut header = &mut header;
        let mut header = header.take_obj_front_zero::<ProtocolRejectHeader>().unwrap();

        header.rejected_protocol = U16::new(self.rejected_protocol);
    }
}

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct EchoDiscardHeader {
    magic_number: U32,
}

impl EchoDiscardHeader {
    pub fn magic_number(&self) -> u32 {
        self.magic_number.get()
    }
}

/// Wrapper around a parsed on-the-wire echo-discard packet header and the rest of the packet.
pub struct EchoDiscardPacket<B> {
    header: LayoutVerified<B, EchoDiscardHeader>,
    body: B,
}

impl<B: ByteSlice> EchoDiscardPacket<B> {
    /// Extract the magic number from the wire format.
    pub fn magic_number(&self) -> u32 {
        self.header.magic_number()
    }
}

impl<B: ByteSlice> ParsablePacket<B, ()> for EchoDiscardPacket<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_packet(self.header.bytes().len(), self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let header = buffer
            .take_obj_front::<EchoDiscardHeader>()
            .ok_or(ParseError::InsufficientHeaderBytes)?;
        Ok(Self { header, body: buffer.into_rest() })
    }
}

/// Builder for an echo-discard packet.
pub struct EchoDiscardPacketBuilder {
    magic_number: u32,
}

impl EchoDiscardPacketBuilder {
    /// Construct with a magic number.
    pub fn new(magic_number: u32) -> Self {
        Self { magic_number }
    }
}

impl PacketBuilder for EchoDiscardPacketBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(std::mem::size_of::<EchoDiscardHeader>(), 0, 0, usize::max_value())
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        let (mut header, _, _) = buffer.parts();
        let mut header = &mut header;
        let mut header = header.take_obj_front_zero::<EchoDiscardHeader>().unwrap();
        header.magic_number = U32::new(self.magic_number);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            ipv4, ipv6, link,
            records::options::{Options, OptionsSerializer},
        },
        packet::{Buf, InnerPacketBuilder, ParseBuffer, Serializer},
        std::sync::Once,
    };

    static START: Once = Once::new();

    #[test]
    fn test_link_parse_serialize() {
        START.call_once(|| {
            fuchsia_syslog::init().unwrap();
        });

        let expected_link_options = [
            link::ControlOption::MaximumReceiveUnit(0x8888),
            link::ControlOption::AddressControlFieldCompression,
            link::ControlOption::QualityProtocol(0xc025, vec![0xab, 0xcd, 0xef]),
            link::ControlOption::MagicNumber(0x01234567),
        ];

        const EXPECT_BUFFER: [u8; 25] = [
            0xc0, 0x21, 3, 0xe6, 0x00, 0x17, 1, 4, 0x05, 0xdc, 8, 2, 4, 7, 0xc0, 0x25, 0xab, 0xcd,
            0xef, 5, 6, 0x01, 0x23, 0x45, 0x67,
        ];

        let buf: [u8; 25] = [
            0xc0, 0x21, 1, 0xe6, 0x00, 0x17, 1, 4, 0x88, 0x88, 8, 2, 4, 7, 0xc0, 0x25, 0xab, 0xcd,
            0xef, 5, 6, 0x01, 0x23, 0x45, 0x67,
        ];

        let mut buf = Buf::new(buf, ..);

        let ppp = buf.parse::<PppPacket<_>>().unwrap();
        let protocol = ppp.protocol();
        assert_eq!(protocol, 0xc021);

        let lcp = buf.parse::<ControlProtocolPacket<_>>().unwrap();
        let code = lcp.code();
        let identifier = lcp.identifier();
        assert_eq!(code, 1);
        assert_eq!(identifier, 0xe6);

        let _configuration_packet = buf.parse::<ConfigurationPacket<_>>().unwrap();
        let mut options: Vec<_> = Options::<_, link::ControlOptionsImpl>::parse(buf.as_ref())
            .map(|options| options.iter().collect())
            .unwrap();

        assert_eq!(options, expected_link_options);

        if let link::ControlOption::MaximumReceiveUnit(ref mut mru) = options[0] {
            *mru = 0x05dc;
        };

        let buffer = OptionsSerializer::<link::ControlOptionsImpl, link::ControlOption, _>::new(
            options.iter(),
        )
        .into_serializer()
        .encapsulate(ControlProtocolPacketBuilder::new(3, identifier))
        .encapsulate(PppPacketBuilder::new(protocol))
        .serialize_vec_outer()
        .ok()
        .unwrap();

        assert_eq!(buffer.as_ref(), EXPECT_BUFFER);
    }

    #[test]
    fn test_ipv4_parse_serialize() {
        START.call_once(|| {
            fuchsia_syslog::init().unwrap();
        });

        let expected_ipv4_options = [
            ipv4::ControlOption::Unrecognized(0xa, vec![0x01, 0x02, 0x03]),
            ipv4::ControlOption::IpAddress(0xffee_ddcc),
            ipv4::ControlOption::IpCompressionProtocol(0x002d, vec![0x89]),
        ];
        const EXPECT_BUFFER: [u8; 22] = [
            0x80, 0x21, 4, 0x03, 0x00, 0x14, 0xa, 5, 0x01, 0x02, 0x03, 0x3, 6, 0xaa, 0xbb, 0xcc,
            0xdd, 0x2, 5, 0x00, 0x2d, 0x89,
        ];

        let buf: [u8; 22] = [
            0x80, 0x21, 1, 0x03, 0x00, 0x14, 0xa, 5, 0x01, 0x02, 0x03, 0x3, 6, 0xff, 0xee, 0xdd,
            0xcc, 0x2, 5, 0x00, 0x2d, 0x89,
        ];

        let mut buf = Buf::new(buf, ..);

        let ppp = buf.parse::<PppPacket<_>>().unwrap();
        let protocol = ppp.protocol();
        assert_eq!(protocol, 0x8021);

        let lcp = buf.parse::<ControlProtocolPacket<_>>().unwrap();
        let code = lcp.code();
        let identifier = lcp.identifier();
        assert_eq!(code, 1);
        assert_eq!(identifier, 0x03);

        let _configuration_packet = buf.parse::<ConfigurationPacket<_>>().unwrap();
        let mut options: Vec<_> = Options::<_, ipv4::ControlOptionsImpl>::parse(buf.as_ref())
            .map(|options| options.iter().collect())
            .unwrap();

        assert_eq!(options, expected_ipv4_options);

        if let ipv4::ControlOption::IpAddress(ref mut address) = options[1] {
            *address = 0xaabbccdd;
        };

        let buffer = OptionsSerializer::<ipv4::ControlOptionsImpl, ipv4::ControlOption, _>::new(
            options.iter(),
        )
        .into_serializer()
        .encapsulate(ControlProtocolPacketBuilder::new(4, identifier))
        .encapsulate(PppPacketBuilder::new(protocol))
        .serialize_vec_outer()
        .ok()
        .unwrap();

        assert_eq!(buffer.as_ref(), EXPECT_BUFFER);
    }

    #[test]
    fn test_ipv6_parse_serialize() {
        START.call_once(|| {
            fuchsia_syslog::init().unwrap();
        });

        let expected_ipv6_options = [
            ipv6::ControlOption::Unrecognized(0x5, vec![0x99]),
            ipv6::ControlOption::InterfaceIdentifier(0x0123_4567_890a_bcde),
        ];

        const EXPECT_BUFFER: [u8; 21] = [
            0x80, 0x57, 2, 0x09, 0x00, 0x13, 0x5, 5, 0x99, 0x88, 0x77, 0x01, 10, 0x01, 0x23, 0x45,
            0x67, 0x89, 0x0a, 0xbc, 0xde,
        ];

        let buf: [u8; 19] = [
            0x80, 0x57, 1, 0x09, 0x00, 0x11, 0x5, 3, 0x99, 0x01, 10, 0x01, 0x23, 0x45, 0x67, 0x89,
            0x0a, 0xbc, 0xde,
        ];

        let mut buf = Buf::new(buf, ..);

        let ppp = buf.parse::<PppPacket<_>>().unwrap();
        let protocol = ppp.protocol();
        assert_eq!(protocol, 0x8057);

        let lcp = buf.parse::<ControlProtocolPacket<_>>().unwrap();
        let code = lcp.code();
        let identifier = lcp.identifier();
        assert_eq!(code, 1);
        assert_eq!(identifier, 0x09);

        let _configuration_packet = buf.parse::<ConfigurationPacket<_>>().unwrap();
        let mut options: Vec<_> = Options::<_, ipv6::ControlOptionsImpl>::parse(buf.as_ref())
            .map(|options| options.iter().collect())
            .unwrap();

        assert_eq!(options, expected_ipv6_options);

        if let ipv6::ControlOption::Unrecognized(ref mut _type, ref mut data) = options[0] {
            data.push(0x88);
            data.push(0x77);
        };

        let buffer = OptionsSerializer::<ipv6::ControlOptionsImpl, ipv6::ControlOption, _>::new(
            options.iter(),
        )
        .into_serializer()
        .encapsulate(ControlProtocolPacketBuilder::new(2, identifier))
        .encapsulate(PppPacketBuilder::new(protocol))
        .serialize_vec_outer()
        .ok()
        .unwrap();

        assert_eq!(buffer.as_ref(), EXPECT_BUFFER);
    }
}
