// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of ARP packets.

#![allow(private_in_public)]

#[cfg(test)]
use std::fmt::{self, Debug, Formatter};
use std::mem;

use byteorder::{ByteOrder, NetworkEndian};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use device::arp::{ArpHardwareType, ArpOp};
use device::ethernet::{EtherType, Mac};
use error::ParseError;
use ip::Ipv4Addr;

// Header has the same memory layout (thanks to repr(C, packed)) as an ARP
// header. Thus, we can simply reinterpret the bytes of the ARP header as a
// Header and then safely access its fields.
// Note the following caveats:
// - We cannot make any guarantees about the alignment of an instance of this
//   struct in memory or of any of its fields. This is true both because
//   repr(packed) removes the padding that would be used to ensure the alignment
//   of individual fields, but also because we are given no guarantees about
//   where within a given memory buffer a particular packet (and thus its
//   header) will be located.
// - Individual fields are all either u8 or [u8; N] rather than u16, u32, etc.
//   This is for two reasons:
//   - u16 and larger have larger-than-1 alignments, which are forbidden as
//     described above
//   - We are not guaranteed that the local platform has the same endianness as
//     network byte order (big endian), so simply treating a sequence of bytes
//     as a u16 or other multi-byte number would not necessarily be correct.
//     Instead, we use the NetworkEndian type and its reader and writer methods
//     to correctly access these fields.
#[derive(Default)]
#[repr(C, packed)]
struct Header {
    htype: [u8; 2], // Hardware (e.g. Ethernet)
    ptype: [u8; 2], // Protocol (e.g. IPv4)
    hlen: u8,       // Length (in octets) of hardware address
    plen: u8,       // Length (in octets) of protocol address
    oper: [u8; 2],  // Operation: 1 for Req, 2 for Reply
}

unsafe impl FromBytes for Header {}
unsafe impl AsBytes for Header {}
unsafe impl Unaligned for Header {}

impl Header {
    fn hardware_protocol(&self) -> u16 {
        NetworkEndian::read_u16(&self.htype)
    }

    fn set_hardware_protocol(&mut self, htype: ArpHardwareType, hlen: u8) -> &mut Self {
        NetworkEndian::write_u16(&mut self.htype, htype as u16);
        self.hlen = hlen;
        self
    }

    fn network_protocol(&self) -> u16 {
        NetworkEndian::read_u16(&self.ptype)
    }

    fn set_network_protocol(&mut self, ptype: EtherType, plen: u8) -> &mut Self {
        NetworkEndian::write_u16(&mut self.ptype, ptype as u16);
        self.plen = plen;
        self
    }

    fn op_code(&self) -> u16 {
        NetworkEndian::read_u16(&self.oper)
    }

    fn set_op_code(&mut self, op: ArpOp) -> &mut Self {
        NetworkEndian::write_u16(&mut self.oper, op as u16);
        self
    }

    fn hardware_address_len(&self) -> u8 {
        self.hlen
    }

    fn protocol_address_len(&self) -> u8 {
        self.plen
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
pub fn peek_arp_types<B: ByteSlice>(bytes: B) -> Result<(ArpHardwareType, EtherType), ParseError> {
    let (header, _) = LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
        debug_err_fn!(ParseError::Format, "too few bytes for header"),
    )?;
    let hw = ArpHardwareType::from_u16(header.hardware_protocol()).ok_or_else(debug_err_fn!(
        ParseError::NotSupported,
        "unrecognized hardware protocol: {:x}",
        header.hardware_protocol()
    ))?;
    let proto = EtherType::from_u16(header.network_protocol()).ok_or_else(debug_err_fn!(
        ParseError::NotSupported,
        "unrecognized network protocol: {:x}",
        header.network_protocol()
    ))?;
    let hlen = match hw {
        ArpHardwareType::Ethernet => <Mac as HType>::hlen(),
    };
    let plen = match proto {
        EtherType::Ipv4 => <Ipv4Addr as PType>::plen(),
        _ => {
            return debug_err!(
                Err(ParseError::NotSupported),
                "unsupported network protocol: {}",
                proto
            )
        }
    };
    if header.hardware_address_len() != hlen || header.protocol_address_len() != plen {
        return debug_err!(
            Err(ParseError::Format),
            "unexpected hardware or protocol address length for protocol {}",
            proto
        );
    }
    Ok((hw, proto))
}

// See comment on Header for an explanation of the memory safety requirements.
#[repr(C, packed)]
struct Body<HwAddr, ProtoAddr> {
    sha: HwAddr,
    spa: ProtoAddr,
    tha: HwAddr,
    tpa: ProtoAddr,
}

unsafe impl<HwAddr: FromBytes, ProtoAddr: FromBytes> FromBytes for Body<HwAddr, ProtoAddr> {}
unsafe impl<HwAddr: AsBytes, ProtoAddr: AsBytes> AsBytes for Body<HwAddr, ProtoAddr> {}
unsafe impl<HwAddr: Unaligned, ProtoAddr: Unaligned> Unaligned for Body<HwAddr, ProtoAddr> {}

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

/// A trait to represent a ARP hardware type.
pub trait HType {
    /// The hardware type.
    fn htype() -> ArpHardwareType;
    /// The in-memory size of an instance of the type.
    fn hlen() -> u8;
}

/// A trait to represent a ARP protocol type.
pub trait PType {
    /// The protocol type.
    fn ptype() -> EtherType;
    /// The in-memory size of an instance of the type.
    fn plen() -> u8;
}

impl HType for Mac {
    fn htype() -> ArpHardwareType {
        ArpHardwareType::Ethernet
    }
    fn hlen() -> u8 {
        use std::convert::TryFrom;
        u8::try_from(mem::size_of::<Mac>()).unwrap()
    }
}

impl PType for Ipv4Addr {
    fn ptype() -> EtherType {
        EtherType::Ipv4
    }
    fn plen() -> u8 {
        use std::convert::TryFrom;
        u8::try_from(mem::size_of::<Ipv4Addr>()).unwrap()
    }
}

/// An ARP packet.
///
/// A `ArpPacket` shares its underlying memory with the byte slice it was parsed
/// from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub struct ArpPacket<B, HwAddr, ProtoAddr> {
    header: LayoutVerified<B, Header>,
    body: LayoutVerified<B, Body<HwAddr, ProtoAddr>>,
}

impl<B: ByteSlice, HwAddr, ProtoAddr> ArpPacket<B, HwAddr, ProtoAddr>
where
    HwAddr: Copy + HType + FromBytes + Unaligned,
    ProtoAddr: Copy + PType + FromBytes + Unaligned,
{
    /// The length of an ARP packet in bytes.
    ///
    /// When calling `ArpPacket::serialize`, provide at least `PACKET_LEN` bytes
    /// for the packet in order to guarantee that `serialize` will not panic.
    ///
    /// `PACKET_LEN` may have different values depending on the type parameters
    /// `HwAddr` and `ProtoAddr`.
    pub const PACKET_LEN: usize =
        mem::size_of::<Header>() + mem::size_of::<Body<HwAddr, ProtoAddr>>();

    /// Parse an ARP packet.
    ///
    /// `parse` parses `bytes` as an ARP packet and validates the header fields.
    ///
    /// If `bytes` are a valid ARP packet, but do not match the hardware address
    /// and protocol address types `HwAddr` and `ProtoAddr`, `parse` will return
    /// `Err(ParseError::NotExpected)`. If multiple hardware or protocol address
    /// types are valid in a given context, `peek_arp_types` may be used to
    /// peek at the header and determine what types are present so that the
    /// correct types can then be used in a call to `parse`.
    ///
    /// The caller may provide more bytes than necessary. This allows the caller
    /// to call `parse` on a payload which was itself padded to meet a minimum
    /// length requirement (for example, for Ethernet frames). See the
    /// `DETAILS.md` file in the repository root for more details.
    pub fn parse(bytes: B) -> Result<ArpPacket<B, HwAddr, ProtoAddr>, ParseError> {
        let (header, body) =
            LayoutVerified::<B, Header>::new_unaligned_from_prefix(bytes).ok_or_else(
                debug_err_fn!(ParseError::Format, "too few bytes for header"),
            )?;
        let (body, _) =
            LayoutVerified::<B, Body<HwAddr, ProtoAddr>>::new_unaligned_from_prefix(body)
                .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for body"))?;

        if header.hardware_protocol() != <HwAddr as HType>::htype() as u16
            || header.network_protocol() != <ProtoAddr as PType>::ptype() as u16
        {
            return debug_err!(
                Err(ParseError::NotExpected),
                "unexpected hardware or network protocols"
            );
        }
        if header.hardware_address_len() != <HwAddr as HType>::hlen()
            || header.protocol_address_len() != <ProtoAddr as PType>::plen()
        {
            return debug_err!(
                Err(ParseError::Format),
                "unexpected hardware or protocol address length"
            );
        }

        if ArpOp::from_u16(header.op_code()).is_none() {
            return debug_err!(
                Err(ParseError::Format),
                "unrecognized op code: {:x}",
                header.op_code()
            );
        }

        Ok(ArpPacket { header, body })
    }

    /// The type of ARP packet
    pub fn operation(&self) -> ArpOp {
        // This is verified in `parse`, so should be safe to unwrap
        ArpOp::from_u16(self.header.op_code()).unwrap()
    }

    /// The hardware address of the ARP packet sender.
    pub fn sender_hardware_address(&self) -> HwAddr {
        self.body.sha
    }

    /// The protocol address of the ARP packet sender.
    pub fn sender_protocol_address(&self) -> ProtoAddr {
        self.body.spa
    }

    /// The hardware address of the ARP packet target.
    pub fn target_hardware_address(&self) -> HwAddr {
        self.body.tha
    }

    /// The protocol address of the ARP packet target.
    pub fn target_protocol_address(&self) -> ProtoAddr {
        self.body.tpa
    }
}

impl<B, HwAddr, ProtoAddr> ArpPacket<B, HwAddr, ProtoAddr>
where
    B: AsMut<[u8]>,
    HwAddr: Copy + HType + FromBytes + AsBytes + Unaligned,
    ProtoAddr: Copy + PType + FromBytes + AsBytes + Unaligned,
{
    /// Serialize an ARP packet in an existing buffer.
    ///
    /// `serialize` serializes an `ArpPacket` which uses the provided `buffer`
    /// for its storage, initializing all header and body fields. If the buffer
    /// is larger than the packet size, it serializes from the beginning of the
    /// buffer, and leaves any remaining bytes at the end of the buffer
    /// untouched. Using a larger-than-necessary buffer can be useful if the
    /// encapsulating protocol has a minimum payload size requirement, and an
    /// ARP packet does not satisfy that minimum.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// ArpPacket::serialize(
    ///     &mut buffer,
    ///     ArpOp::Request,
    ///     src_mac,
    ///     src_ip,
    ///     dst_mac,
    ///     dst_ip,
    /// );
    /// ```
    ///
    /// # Panics
    ///
    /// `serialize` panics if there is insufficient room in the buffer to store
    /// the ARP packet. The caller can guarantee that there will be enough room
    /// by providing a buffer of at least `ArpPacket::MAX_LEN` bytes.
    pub fn serialize(
        mut buffer: B, operation: ArpOp, sender_hardware_addr: HwAddr,
        sender_protocol_addr: ProtoAddr, target_hardware_addr: HwAddr,
        target_protocol_addr: ProtoAddr,
    ) {
        // SECURITY: Use _zeroed constructors to ensure we zero memory to
        // prevent leaking information from packets previously stored in
        // this buffer.
        let (mut header, rest) =
            LayoutVerified::<_, Header>::new_unaligned_from_prefix_zeroed(buffer.as_mut())
                .expect("not enough bytes for an ARP packet");
        let (mut body, _) =
            LayoutVerified::<_, Body<HwAddr, ProtoAddr>>::new_unaligned_from_prefix_zeroed(rest)
                .expect("not enough bytes for an ARP packet");

        header
            .set_hardware_protocol(<HwAddr as HType>::htype(), <HwAddr as HType>::hlen())
            .set_network_protocol(<ProtoAddr as PType>::ptype(), <ProtoAddr as PType>::plen())
            .set_op_code(operation);

        body.set_sha(sender_hardware_addr)
            .set_spa(sender_protocol_addr)
            .set_tha(target_hardware_addr)
            .set_tpa(target_protocol_addr);
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
    use super::*;
    use ip::Ipv4Addr;
    use wire::ethernet::EthernetFrame;

    const TEST_SENDER_IPV4: Ipv4Addr = Ipv4Addr::new([1, 2, 3, 4]);
    const TEST_TARGET_IPV4: Ipv4Addr = Ipv4Addr::new([5, 6, 7, 8]);
    const TEST_SENDER_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const TEST_TARGET_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    #[test]
    fn test_parse_full() {
        use wire::testdata::*;

        let (frame, _) = EthernetFrame::parse(ARP_REQUEST).unwrap();
        assert_eq!(frame.ethertype(), Some(Ok(EtherType::Arp)));

        let (hw, proto) = peek_arp_types(frame.body()).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);
        let arp = ArpPacket::<_, Mac, Ipv4Addr>::parse(frame.body()).unwrap();
        assert_eq!(arp.operation(), ArpOp::Request);
        assert_eq!(frame.src_mac(), arp.sender_hardware_address()); // These will be the same
    }

    fn header_to_bytes(header: Header) -> [u8; 8] {
        let mut bytes = [0; 8];
        {
            let mut lv = LayoutVerified::new_unaligned(&mut bytes[..]).unwrap();
            *lv = header;
        }
        bytes
    }

    // Return a new Header for an Ethernet/IPv4 ARP request.
    fn new_header() -> Header {
        let mut header = Header::default();
        header.set_hardware_protocol(<Mac as HType>::htype(), <Mac as HType>::hlen());
        header.set_network_protocol(<Ipv4Addr as PType>::ptype(), <Ipv4Addr as PType>::plen());
        header.set_op_code(ArpOp::Request);
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
        NetworkEndian::write_u16(&mut header.oper[..], 3);
        let (hw, proto) = peek_arp_types(&header_to_bytes(header)[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);
    }

    #[test]
    fn test_parse() {
        let mut bytes = [
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 5, 6, 7, 8,
        ];
        (&mut bytes[..8]).copy_from_slice(&header_to_bytes(new_header()));
        let (hw, proto) = peek_arp_types(&bytes[..]).unwrap();
        assert_eq!(hw, ArpHardwareType::Ethernet);
        assert_eq!(proto, EtherType::Ipv4);
        let packet = ArpPacket::<_, Mac, Ipv4Addr>::parse(&bytes[..]).unwrap();
        assert_eq!(packet.sender_hardware_address(), TEST_SENDER_MAC);
        assert_eq!(packet.sender_protocol_address(), TEST_SENDER_IPV4);
        assert_eq!(packet.target_hardware_address(), TEST_TARGET_MAC);
        assert_eq!(packet.target_protocol_address(), TEST_TARGET_IPV4);
        assert_eq!(packet.operation(), ArpOp::Request);
    }

    #[test]
    fn test_serialize() {
        let mut buf = [0; 28];
        {
            ArpPacket::serialize(
                &mut buf[..],
                ArpOp::Request,
                TEST_SENDER_MAC,
                TEST_SENDER_IPV4,
                TEST_TARGET_MAC,
                TEST_TARGET_IPV4,
            );
        }
        assert_eq!(
            buf,
            [
                0, 1, 8, 0, 6, 4, 0, 1, 0, 1, 2, 3, 4, 5, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 5, 6, 7,
                8,
            ]
        );
        let packet = ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..28]).unwrap();
        assert_eq!(packet.sender_hardware_address(), TEST_SENDER_MAC);
        assert_eq!(packet.sender_protocol_address(), TEST_SENDER_IPV4);
        assert_eq!(packet.target_hardware_address(), TEST_TARGET_MAC);
        assert_eq!(packet.target_protocol_address(), TEST_TARGET_IPV4);
        assert_eq!(packet.operation(), ArpOp::Request);
    }

    #[test]
    fn test_peek_error() {
        // Test that a header which is too short is rejected.
        let buf = [0; 7];
        assert_eq!(peek_arp_types(&buf[..]).unwrap_err(), ParseError::Format);

        // Test that an unexpected hardware protocol type is rejected.
        let mut header = new_header();
        NetworkEndian::write_u16(&mut header.htype[..], 0);
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::NotSupported
        );

        // Test that an unexpected network protocol type is rejected.
        let mut header = new_header();
        NetworkEndian::write_u16(&mut header.ptype[..], 0);
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::NotSupported
        );

        // Test that an incorrect hardware address len is rejected.
        let mut header = new_header();
        header.hlen = 7;
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::Format
        );

        // Test that an incorrect protocol address len is rejected.
        let mut header = new_header();
        header.plen = 5;
        assert_eq!(
            peek_arp_types(&header_to_bytes(header)[..]).unwrap_err(),
            ParseError::Format
        );
    }

    #[test]
    fn test_parse_error() {
        // Test that a packet which is too short is rejected.
        let buf = [0; 27];
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::Format
        );

        let mut buf = [0; 28];

        // Test that an unexpected hardware protocol type is rejected.
        let mut header = new_header();
        NetworkEndian::write_u16(&mut header.htype[..], 0);
        (&mut buf[..8]).copy_from_slice(&header_to_bytes(header)[..]);
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::NotExpected
        );

        // Test that an unexpected network protocol type is rejected.
        let mut header = new_header();
        NetworkEndian::write_u16(&mut header.ptype[..], 0);
        (&mut buf[..8]).copy_from_slice(&header_to_bytes(header)[..]);
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::NotExpected
        );

        // Test that an incorrect hardware address len is rejected.
        let mut header = new_header();
        header.hlen = 7;
        (&mut buf[..8]).copy_from_slice(&header_to_bytes(header)[..]);
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::Format
        );

        // Test that an incorrect protocol address len is rejected.
        let mut header = new_header();
        header.plen = 5;
        (&mut buf[..8]).copy_from_slice(&header_to_bytes(header)[..]);
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::Format
        );

        // Test that an invalid operation is rejected.
        let mut header = new_header();
        NetworkEndian::write_u16(&mut header.oper[..], 3);
        (&mut buf[..8]).copy_from_slice(&header_to_bytes(header)[..]);
        assert_eq!(
            ArpPacket::<_, Mac, Ipv4Addr>::parse(&buf[..]).unwrap_err(),
            ParseError::Format
        );
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that ArpPacket::serialize properly zeroes memory before
        // serializing the packet.
        let mut buf_0 = [0; 28];
        ArpPacket::serialize(
            &mut buf_0[..],
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        );
        let mut buf_1 = [0xFF; 28];
        ArpPacket::serialize(
            &mut buf_1[..],
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        );
        assert_eq!(buf_0, buf_1);
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic_insufficient_packet_space() {
        // Test that a buffer which doesn't leave enough room for the packet is
        // rejected.
        ArpPacket::serialize(
            &mut [0; 27],
            ArpOp::Request,
            TEST_SENDER_MAC,
            TEST_SENDER_IPV4,
            TEST_TARGET_MAC,
            TEST_TARGET_IPV4,
        );
    }
}
