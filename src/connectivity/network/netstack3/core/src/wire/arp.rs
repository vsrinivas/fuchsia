// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of ARP packets.

#![allow(private_in_public)]

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::mem;

use net_types::ethernet::Mac;
use net_types::ip::Ipv4Addr;
use packet::{BufferView, BufferViewMut, InnerPacketBuilder, ParsablePacket, ParseMetadata};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::device::arp::{ArpHardwareType, ArpOp, HType, PType};
use crate::device::ethernet::EtherType;
use crate::error::{ParseError, ParseResult};
use crate::wire::U16;

#[cfg(test)]
pub(crate) const ARP_HDR_LEN: usize = 8;
#[cfg(test)]
pub(crate) const ARP_ETHERNET_IPV4_PACKET_LEN: usize = 28;

#[derive(Default, FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct Header {
    htype: U16, // Hardware (e.g. Ethernet)
    ptype: U16, // Protocol (e.g. IPv4)
    hlen: u8,   // Length (in octets) of hardware address
    plen: u8,   // Length (in octets) of protocol address
    oper: U16,  // Operation: 1 for Req, 2 for Reply
}

impl Header {
    fn set_htype(&mut self, htype: ArpHardwareType, hlen: u8) -> &mut Header {
        self.htype = U16::new(htype as u16);
        self.hlen = hlen;
        self
    }

    fn set_ptype(&mut self, ptype: EtherType, plen: u8) -> &mut Header {
        self.ptype = U16::new(ptype.into());
        self.plen = plen;
        self
    }

    fn set_op(&mut self, op: ArpOp) -> &mut Header {
        self.oper = U16::new(op as u16);
        self
    }
}

/// Peek at an ARP header to see what hardware and protocol address types are
/// used.
///
/// Since `ArpPacket` is statically typed with the hardware and protocol address
/// types expected in the header and body, these types must be known ahead of
/// time before calling `parse`. If multiple different types are valid in a
/// given parsing context, and so the caller cannot know ahead of time which
/// types to use, `peek_arp_types` can be used to peek at the header first to
/// figure out which static types should be used in a subsequent call to
/// `parse`.
///
/// Note that `peek_arp_types` only inspects certain fields in the header, and
/// so `peek_arp_types` succeeding does not guarantee that a subsequent call to
/// `parse` will also succeed.
pub(crate) fn peek_arp_types<B: ByteSlice>(bytes: B) -> ParseResult<(ArpHardwareType, EtherType)> {
    let (header, _) = LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes)
        .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
    let hw = ArpHardwareType::from_u16(header.htype.get()).ok_or_else(debug_err_fn!(
        ParseError::NotSupported,
        "unrecognized hardware protocol: {:x}",
        header.htype.get()
    ))?;
    let proto = EtherType::from(header.ptype.get());
    let hlen = match hw {
        ArpHardwareType::Ethernet => <Mac as HType>::HLEN,
    };
    let plen = match proto {
        EtherType::Ipv4 => <Ipv4Addr as PType>::PLEN,
        _ => {
            return debug_err!(
                Err(ParseError::NotSupported),
                "unsupported network protocol: {}",
                proto
            );
        }
    };
    if header.hlen != hlen || header.plen != plen {
        return debug_err!(
            Err(ParseError::Format),
            "unexpected hardware or protocol address length for protocol {}",
            proto
        );
    }
    Ok((hw, proto))
}

// Body has the same memory layout (thanks to repr(C)) as an ARP body. Thus, we
// can simply reinterpret the bytes of the ARP body as a Body and then safely
// access its fields. Note the following caveats:
// - While FromBytes and Unaligned are taken care of for us by their derives, we
//   still have to manually verify the correctness of our AsBytes
//   implementation. The AsBytes implementation is sound if all of the fields
//   are themselves AsBytes and there is no padding. We are guaranteed no
//   padding so long as each field also has no alignment requirement that would
//   cause the layout algorithm to produce padding. Thus, we use an AsBytes +
//   Unaligned bound for our type parameters.
#[derive(FromBytes, Unaligned)]
#[repr(C)]
struct Body<HwAddr, ProtoAddr> {
    sha: HwAddr,
    spa: ProtoAddr,
    tha: HwAddr,
    tpa: ProtoAddr,
}

unsafe impl<HwAddr: AsBytes + Unaligned, ProtoAddr: AsBytes + Unaligned> AsBytes
    for Body<HwAddr, ProtoAddr>
{
    // We're doing a bad thing, but it's necessary until derive(AsBytes)
    // supports type parameters.
    fn only_derive_is_allowed_to_implement_this_trait() {}
}

impl<HwAddr: Copy, ProtoAddr: Copy> Body<HwAddr, ProtoAddr> {
    fn set_sha(&mut self, sha: HwAddr) -> &mut Self {
        self.sha = sha;
        self
    }

    fn set_spa(&mut self, spa: ProtoAddr) -> &mut Self {
        self.spa = spa;
        self
    }

    fn set_tha(&mut self, tha: HwAddr) -> &mut Self {
        self.tha = tha;
        self
    }

    fn set_tpa(&mut self, tpa: ProtoAddr) -> &mut Self {
        self.tpa = tpa;
        self
    }
}

/// An ARP packet.
///
/// A `ArpPacket` shares its underlying memory with the byte slice it was parsed
/// from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub(crate) struct ArpPacket<B, HwAddr, ProtoAddr> {
    header: LayoutVerified<B, Header>,
    body: LayoutVerified<B, Body<HwAddr, ProtoAddr>>,
}

impl<B: ByteSlice, HwAddr, ProtoAddr> ParsablePacket<B, ()> for ArpPacket<B, HwAddr, ProtoAddr>
where
    HwAddr: Copy + HType + FromBytes + Unaligned,
    ProtoAddr: Copy + PType + FromBytes + Unaligned,
{
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        ParseMetadata::from_inner_packet(self.header.bytes().len() + self.body.bytes().len())
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, _args: ()) -> ParseResult<Self> {
        let header = buffer
            .take_obj_front::<Header>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        let body = buffer
            .take_obj_front::<Body<HwAddr, ProtoAddr>>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for body"))?;
        // Consume any padding bytes added by the previous layer.
        buffer.take_rest_front();

        if header.htype.get() != <HwAddr as HType>::HTYPE as u16
            || header.ptype.get() != <ProtoAddr as PType>::PTYPE.into()
        {
            return debug_err!(
                Err(ParseError::NotExpected),
                "unexpected hardware or network protocols"
            );
        }
        if header.hlen != <HwAddr as HType>::HLEN || header.plen != <ProtoAddr as PType>::PLEN {
            return debug_err!(
                Err(ParseError::Format),
                "unexpected hardware or protocol address length"
            );
        }

        if ArpOp::from_u16(header.oper.get()).is_none() {
            return debug_err!(
                Err(ParseError::Format),
                "unrecognized op code: {:x}",
                header.oper.get()
            );
        }

        Ok(ArpPacket { header, body })
    }
}

impl<B: ByteSlice, HwAddr, ProtoAddr> ArpPacket<B, HwAddr, ProtoAddr>
where
    HwAddr: Copy + HType + FromBytes + Unaligned,
    ProtoAddr: Copy + PType + FromBytes + Unaligned,
{
    /// The type of ARP packet
    pub(crate) fn operation(&self) -> ArpOp {
        // This is verified in `parse`, so should be safe to unwrap
        ArpOp::from_u16(self.header.oper.get()).unwrap()
    }

    /// The hardware address of the ARP packet sender.
    pub(crate) fn sender_hardware_address(&self) -> HwAddr {
        self.body.sha
    }

    /// The protocol address of the ARP packet sender.
    pub(crate) fn sender_protocol_address(&self) -> ProtoAddr {
        self.body.spa
    }

    /// The hardware address of the ARP packet target.
    pub(crate) fn target_hardware_address(&self) -> HwAddr {
        self.body.tha
    }

    /// The protocol address of the ARP packet target.
    pub(crate) fn target_protocol_address(&self) -> ProtoAddr {
        self.body.tpa
    }

    /// Construct a builder with the same contents as this packet.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    pub(crate) fn builder(&self) -> ArpPacketBuilder<HwAddr, ProtoAddr> {
        ArpPacketBuilder {
            op: self.operation(),
            sha: self.sender_hardware_address(),
            spa: self.sender_protocol_address(),
            tha: self.target_hardware_address(),
            tpa: self.target_protocol_address(),
        }
    }
}

/// A builder for ARP packets.
#[derive(Debug)]
pub(crate) struct ArpPacketBuilder<HwAddr, ProtoAddr> {
    op: ArpOp,
    sha: HwAddr,
    spa: ProtoAddr,
    tha: HwAddr,
    tpa: ProtoAddr,
}

impl<HwAddr, ProtoAddr> ArpPacketBuilder<HwAddr, ProtoAddr> {
    /// Construct a new `ArpPacketBuilder`.
    pub(crate) fn new(
        operation: ArpOp,
        sender_hardware_addr: HwAddr,
        sender_protocol_addr: ProtoAddr,
        target_hardware_addr: HwAddr,
        target_protocol_addr: ProtoAddr,
    ) -> ArpPacketBuilder<HwAddr, ProtoAddr> {
        ArpPacketBuilder {
            op: operation,
            sha: sender_hardware_addr,
            spa: sender_protocol_addr,
            tha: target_hardware_addr,
            tpa: target_protocol_addr,
        }
    }
}

impl<HwAddr, ProtoAddr> InnerPacketBuilder for ArpPacketBuilder<HwAddr, ProtoAddr>
where
    HwAddr: Copy + HType + FromBytes + AsBytes + Unaligned,
    ProtoAddr: Copy + PType + FromBytes + AsBytes + Unaligned,
{
    fn bytes_len(&self) -> usize {
        mem::size_of::<Header>() + mem::size_of::<Body<HwAddr, ProtoAddr>>()
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut buffer = &mut buffer;

        // SECURITY: Use _zero constructors to ensure we zero memory to prevent
        // leaking information from packets previously stored in this buffer.
        let mut header =
            buffer.take_obj_front_zero::<Header>().expect("not enough bytes for an ARP packet");
        let mut body = buffer
            .take_obj_front_zero::<Body<HwAddr, ProtoAddr>>()
            .expect("not enough bytes for an ARP packet");
        header
            .set_htype(<HwAddr as HType>::HTYPE, <HwAddr as HType>::HLEN)
            .set_ptype(<ProtoAddr as PType>::PTYPE, <ProtoAddr as PType>::PLEN)
            .set_op(self.op);
        body.set_sha(self.sha).set_spa(self.spa).set_tha(self.tha).set_tpa(self.tpa);
    }
}

#[cfg(test)]
impl<B, HwAddr, ProtoAddr> Debug for ArpPacket<B, HwAddr, ProtoAddr> {
    fn fmt(&self, fmt: &mut Formatter) -> fmt::Result {
        write!(fmt, "ArpPacket")
    }
}

#[cfg(test)]
mod tests {
    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};

    use super::*;
    use crate::testutil::*;
    use crate::wire::ethernet::EthernetFrame;

    const TEST_SENDER_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_TARGET_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SENDER_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_TARGET_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    #[test]
    fn test_parse_serialize_full() {
        use crate::wire::testdata::arp_request::*;

        let mut buf = ETHERNET_FRAME.bytes;
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        verify_ethernet_frame(&frame, ETHERNET_FRAME);

        let (hw, proto) = peek_arp_types(frame.body()).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        let mut body = frame.body();
        let arp = body.parse::<ArpPacket<_, Mac, Ipv4Addr>>().unwrap();
        assert_eq!(arp.operation(), ARP_OPERATION);
        assert_eq!(frame.src_mac(), arp.sender_hardware_address());

        let frame_bytes = arp
            .builder()
            .into_serializer()
            .encapsulate(frame.builder())
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(frame_bytes.as_ref(), ETHERNET_FRAME.bytes);
    }

    fn header_to_bytes(header: Header) -> [u8; ARP_HDR_LEN] {
        let mut bytes = [0; ARP_HDR_LEN];
        {
            let mut lv = LayoutVerified::<_, Header>::new_unaligned(&mut bytes[..]).unwrap();
            *lv = header;
        }
        bytes
    }

    // Return a new Header for an Ethernet/IPv4 ARP request.
    fn new_header() -> Header {
        let mut header = Header::default();
        header
            .set_htype(<Mac as HType>::HTYPE, <Mac as HType>::HLEN)
            .set_ptype(<Ipv4Addr as PType>::PTYPE, <Ipv4Addr as PType>::PLEN)
            .set_op(ArpOp::Request);
        header
    }

    #[test]
    fn test_peek() {
        let header = new_header();
        let (hw, proto) = peek_arp_types(&header_to_bytes(header)[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        // Test that an invalid operation is not rejected; peek_arp_types does
        // not inspect the operation.
        let mut header = new_header();
        header.oper = U16::new(3);
        let (hw, proto) = peek_arp_types(&header_to_bytes(header)[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);
    }

    #[test]
    fn test_parse() {
        let mut buf = &mut [
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 5, 6, 7, 8,
        ][..];
        (&mut buf[..ARP_HDR_LEN]).copy_from_slice(&header_to_bytes(new_header()));
        let (hw, proto) = peek_arp_types(&buf[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);

        let buf = &mut buf;
        let packet = buf.parse::<ArpPacket<_, Mac, Ipv4Addr>>().unwrap();
        assert_eq!(packet.sender_hardware_address(), TEST_SENDER_MAC);
        assert_eq!(packet.sender_protocol_address(), TEST_SENDER_IPV4);
        assert_eq!(packet.target_hardware_address(), TEST_TARGET_MAC);
        assert_eq!(packet.target_protocol_address(), TEST_TARGET_IPV4);
        assert_eq!(packet.operation(), ArpOp::Request);
    }

    #[test]
    fn test_serialize() {
        let mut buf = ArpPacketBuilder::new(
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        )
        .into_serializer()
        .serialize_vec_outer()
        .unwrap();
        assert_eq!(
            AsRef::<[u8]>::as_ref(&buf),
            &[0, 1, 8, 0, 6, 4, 0, 1, 0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 5, 6, 7, 8,]
        );
        let packet = buf.parse::<ArpPacket<_, Mac, Ipv4Addr>>().unwrap();
        assert_eq!(packet.sender_hardware_address(), TEST_SENDER_MAC);
        assert_eq!(packet.sender_protocol_address(), TEST_SENDER_IPV4);
        assert_eq!(packet.target_hardware_address(), TEST_TARGET_MAC);
        assert_eq!(packet.target_protocol_address(), TEST_TARGET_IPV4);
        assert_eq!(packet.operation(), ArpOp::Request);
    }

    #[test]
    fn test_peek_error() {
        // Test that a header which is too short is rejected.
        let buf = [0; ARP_HDR_LEN - 1];
        assert_eq!(peek_arp_types(&buf[..]).unwrap_err(), ParseError::Format);

        // Test that an unexpected hardware protocol type is rejected.
        let mut header = new_header();
        header.htype = U16::ZERO;
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::NotSupported
        );

        // Test that an unexpected network protocol type is rejected.
        let mut header = new_header();
        header.ptype = U16::ZERO;
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::NotSupported
        );

        // Test that an incorrect hardware address len is rejected.
        let mut header = new_header();
        header.hlen = 7;
        assert_eq!(peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(), ParseError::Format);

        // Test that an incorrect protocol address len is rejected.
        let mut header = new_header();
        header.plen = 5;
        assert_eq!(peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(), ParseError::Format);
    }

    #[test]
    fn test_parse_error() {
        // Assert that parsing a buffer results in an error.
        fn assert_err(mut buf: &[u8], err: ParseError) {
            assert_eq!(buf.parse::<ArpPacket<_, Mac, Ipv4Addr>>().unwrap_err(), err);
        }

        // Assert that parsing a particular header results in an error.
        fn assert_header_err(header: Header, err: ParseError) {
            let mut buf = [0; ARP_ETHERNET_IPV4_PACKET_LEN];
            *LayoutVerified::<_, Header>::new_unaligned_from_prefix(&mut buf[..]).unwrap().0 =
                header;
            assert_err(&buf[..], err);
        }

        // Test that a packet which is too short is rejected.
        let buf = [0; ARP_ETHERNET_IPV4_PACKET_LEN - 1];
        assert_err(&buf[..], ParseError::Format);

        // Test that an unexpected hardware protocol type is rejected.
        let mut header = new_header();
        header.htype = U16::ZERO;
        assert_header_err(header, ParseError::NotExpected);

        // Test that an unexpected network protocol type is rejected.
        let mut header = new_header();
        header.ptype = U16::ZERO;
        assert_header_err(header, ParseError::NotExpected);

        // Test that an incorrect hardware address len is rejected.
        let mut header = new_header();
        header.hlen = 7;
        assert_header_err(header, ParseError::Format);

        // Test that an incorrect protocol address len is rejected.
        let mut header = new_header();
        header.plen = 5;
        assert_header_err(header, ParseError::Format);

        // Test that an invalid operation is rejected.
        let mut header = new_header();
        header.oper = U16::new(3);
        assert_header_err(header, ParseError::Format);
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that ArpPacket::serialize properly zeroes memory before
        // serializing the packet.
        let mut buf_0 = [0; ARP_ETHERNET_IPV4_PACKET_LEN];
        ArpPacketBuilder::new(
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        )
        .serialize(&mut buf_0[..]);
        let mut buf_1 = [0xFF; ARP_ETHERNET_IPV4_PACKET_LEN];
        ArpPacketBuilder::new(
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        )
        .serialize(&mut buf_1[..]);
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic(expected = "not enough bytes for an ARP packet")]
    fn test_serialize_panic_insufficient_packet_space() {
        // Test that a buffer which doesn't leave enough room for the packet is
        // rejected.
        ArpPacketBuilder::new(
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        )
        .serialize(&mut [0; ARP_ETHERNET_IPV4_PACKET_LEN - 1]);
    }
}
