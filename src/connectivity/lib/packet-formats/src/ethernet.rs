// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Ethernet frames.

use net_types::ethernet::Mac;
use net_types::ip::{Ip, Ipv4, Ipv6};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, PacketConstraints, ParsablePacket, ParseMetadata,
    SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use self::inner::*;
use crate::error::{ParseError, ParseResult};
use crate::{U16, U32};

const ETHERNET_MIN_ILLEGAL_ETHERTYPE: u16 = 1501;
const ETHERNET_MAX_ILLEGAL_ETHERTYPE: u16 = 1535;

create_protocol_enum!(
    /// An EtherType number.
    #[allow(missing_docs)]
    #[derive(Copy, Clone, Hash, Eq, PartialEq)]
    pub enum EtherType: u16 {
        Ipv4, 0x0800, "IPv4";
        Arp, 0x0806, "ARP";
        Ipv6, 0x86DD, "IPv6";
        _, "EtherType {}";
    }
);

/// An extension trait adding IP-related functionality to `Ipv4` and `Ipv6`.
pub trait EthernetIpExt: Ip {
    /// The `EtherType` value for an associated IP version.
    const ETHER_TYPE: EtherType;
}

impl<I: Ip> EthernetIpExt for I {
    default const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv4 {
    const ETHER_TYPE: EtherType = EtherType::Ipv4;
}

impl EthernetIpExt for Ipv6 {
    const ETHER_TYPE: EtherType = EtherType::Ipv6;
}

#[derive(FromBytes, AsBytes, Unaligned)]
#[repr(C)]
struct HeaderPrefix {
    dst_mac: Mac,
    src_mac: Mac,
}

const TPID_8021Q: u16 = 0x8100;
const TPID_8021AD: u16 = 0x88a8;

/// An Ethernet frame.
///
/// An `EthernetFrame` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub struct EthernetFrame<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    tag: Option<LayoutVerified<B, U32>>,
    ethertype: LayoutVerified<B, U16>,
    body: B,
}

/// Whether or not an Ethernet frame's length should be checked during parsing.
///
/// When the `Check` variant is used, the Ethernet frame will be rejected if its
/// total length (including header, but excluding the Frame Check Sequence (FCS)
/// footer) is less than the required minimum of 60 bytes.
#[derive(PartialEq)]
pub enum EthernetFrameLengthCheck {
    /// Check that the Ethernet frame's total length (including header, but
    /// excluding the Frame Check Sequence (FCS) footer) satisfies the required
    /// minimum of 60 bytes.
    Check,
    /// Do not check the Ethernet frame's total length. The frame will still be
    /// rejected if a complete, valid header is not present, but the body may be
    /// 0 bytes long.
    NoCheck,
}

impl<B: ByteSlice> ParsablePacket<B, EthernetFrameLengthCheck> for EthernetFrame<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len()
            + self.tag.as_ref().map(|tag| tag.bytes().len()).unwrap_or(0)
            + self.ethertype.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(
        mut buffer: BV,
        length_check: EthernetFrameLengthCheck,
    ) -> ParseResult<Self> {
        // See for details: https://en.wikipedia.org/wiki/Ethernet_frame#Frame_%E2%80%93_data_link_layer

        let hdr_prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?;
        if length_check == EthernetFrameLengthCheck::Check && buffer.len() < 48 {
            // The minimum frame size (not including the Frame Check Sequence
            // (FCS) footer, which we do not handle in this code) is 60 bytes.
            // We've already consumed 12 bytes for the header prefix, so we must
            // have at least 48 bytes left.
            return debug_err!(Err(ParseError::Format), "too few bytes for frame");
        }

        // The tag (either IEEE 802.1Q or 802.1ad) is an optional four-byte
        // field. If present, it precedes the ethertype, and its first two bytes
        // (where the ethertype bytes are normally) are called the Tag Protocol
        // Identifier (TPID). A TPID of TPID_8021Q implies an 802.1Q tag, a TPID
        // of TPID_8021AD implies an 802.1ad tag, and anything else implies that
        // there is no tag - it's a normal ethertype field.
        let ethertype_or_tpid = buffer
            .peek_obj_front::<U16>()
            .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?
            .get();
        let (tag, ethertype, body) = match ethertype_or_tpid {
            self::TPID_8021Q | self::TPID_8021AD => (
                Some(
                    buffer.take_obj_front().ok_or_else(debug_err_fn!(
                        ParseError::Format,
                        "too few bytes for header"
                    ))?,
                ),
                buffer
                    .take_obj_front()
                    .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?,
                buffer.into_rest(),
            ),
            _ => (
                None,
                buffer
                    .take_obj_front()
                    .ok_or_else(debug_err_fn!(ParseError::Format, "too few bytes for header"))?,
                buffer.into_rest(),
            ),
        };

        let frame = EthernetFrame { hdr_prefix, tag, ethertype, body };
        let et = frame.ethertype.get();
        if (ETHERNET_MIN_ILLEGAL_ETHERTYPE..=ETHERNET_MAX_ILLEGAL_ETHERTYPE).contains(&et)
            || (et < ETHERNET_MIN_ILLEGAL_ETHERTYPE && et as usize != frame.body.len())
        {
            // EtherType values between 1500 and 1536 are disallowed, and values
            // of 1500 and below are used to indicate the body length.
            return debug_err!(Err(ParseError::Format), "invalid ethertype number: {:x}", et);
        }
        Ok(frame)
    }
}

impl<B: ByteSlice> EthernetFrame<B> {
    /// The frame body.
    pub fn body(&self) -> &[u8] {
        &self.body
    }

    /// The source MAC address.
    pub fn src_mac(&self) -> Mac {
        self.hdr_prefix.src_mac
    }

    /// The destination MAC address.
    pub fn dst_mac(&self) -> Mac {
        self.hdr_prefix.dst_mac
    }

    /// The EtherType.
    ///
    /// `ethertype` returns the `EtherType` from the Ethernet header. However,
    /// some values of the EtherType header field are used to indicate the
    /// length of the frame's body. In this case, `ethertype` returns `None`.
    pub fn ethertype(&self) -> Option<EtherType> {
        let et = self.ethertype.get();
        if et < ETHERNET_MIN_ILLEGAL_ETHERTYPE {
            return None;
        }
        // values in (1500, 1536) are illegal, and shouldn't make it through
        // parse
        debug_assert!(et > ETHERNET_MAX_ILLEGAL_ETHERTYPE);
        Some(EtherType::from(et))
    }

    // The size of the frame header.
    fn header_len(&self) -> usize {
        self.hdr_prefix.bytes().len()
            + self.tag.as_ref().map(|t| t.bytes().len()).unwrap_or(0)
            + self.ethertype.bytes().len()
    }

    // Total frame length including header prefix, tag, EtherType, and body.
    // This is not the same as the length as optionally encoded in the
    // EtherType.
    // TODO(rheacock): remove `allow(dead_code)` when this is used.
    #[allow(dead_code)]
    fn total_frame_len(&self) -> usize {
        self.header_len() + self.body.len()
    }

    /// Construct a builder with the same contents as this frame.
    pub fn builder(&self) -> EthernetFrameBuilder {
        EthernetFrameBuilder {
            src_mac: self.src_mac(),
            dst_mac: self.dst_mac(),
            ethertype: self.ethertype.get(),
        }
    }
}

/// A builder for Ethernet frames.
#[derive(Debug)]
pub struct EthernetFrameBuilder {
    src_mac: Mac,
    dst_mac: Mac,
    ethertype: u16,
}

impl EthernetFrameBuilder {
    /// Construct a new `EthernetFrameBuilder`.
    pub fn new(src_mac: Mac, dst_mac: Mac, ethertype: EtherType) -> EthernetFrameBuilder {
        EthernetFrameBuilder { src_mac, dst_mac, ethertype: ethertype.into() }
    }
}

// NOTE(joshlf): header_len and min_body_len assume no 802.1Q or 802.1ad tag. We
// don't support creating packets with these tags at the moment, so this is a
// sound assumption. If we support them in the future, we will need to update
// these to compute dynamically.

impl PacketBuilder for EthernetFrameBuilder {
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints::new(
            ETHERNET_HDR_LEN_NO_TAG,
            0,
            ETHERNET_MIN_BODY_LEN_NO_TAG,
            core::usize::MAX,
        )
    }

    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        // NOTE: EtherType values of 1500 and below are used to indicate the
        // length of the body in bytes. We don't need to validate this because
        // the EtherType enum has no variants with values in that range.

        let total_len = buffer.header().len() + buffer.body().len();
        let mut header = buffer.header();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        header
            .write_obj_front(&HeaderPrefix { src_mac: self.src_mac, dst_mac: self.dst_mac })
            .expect("too few bytes for Ethernet header");
        header
            .write_obj_front(&U16::new(self.ethertype))
            .expect("too few bytes for Ethernet header");

        // NOTE(joshlf): This doesn't include the tag. If we ever add support
        // for serializing tags, we will need to update this.
        let min_frame_size = ETHERNET_MIN_BODY_LEN_NO_TAG
            + core::mem::size_of::<HeaderPrefix>()
            + core::mem::size_of::<U16>();

        // Assert this here so that if there isn't enough space for even an
        // Ethernet header, we report that more specific error.
        assert!(
            total_len >= min_frame_size,
            "total frame size of {} bytes is below minimum frame size of {}",
            total_len,
            min_frame_size,
        );
    }
}

/// A private module used to make sure that its contents are only accessible from
/// the parent module and its `testutil` module.
mod inner {
    /// The length of an Ethernet header when it has no tags.
    pub const ETHERNET_HDR_LEN_NO_TAG: usize = 14;

    /// The minimum length of an Ethernet frame's body when the header contains no tags.
    pub const ETHERNET_MIN_BODY_LEN_NO_TAG: usize = 46;
}

/// Constants useful for testing.
pub mod testutil {
    pub use super::inner::{ETHERNET_HDR_LEN_NO_TAG, ETHERNET_MIN_BODY_LEN_NO_TAG};

    /// Ethernet frame, in bytes.
    pub const ETHERNET_DST_MAC_BYTE_OFFSET: usize = 0;

    /// The offset to the start of the source MAC address from the start of the
    /// Ethernet frame, in bytes.
    pub const ETHERNET_SRC_MAC_BYTE_OFFSET: usize = 6;
}

#[cfg(test)]
mod tests {
    use packet::{
        AsFragmentedByteSlice, Buf, InnerPacketBuilder, ParseBuffer, SerializeBuffer, Serializer,
    };
    use zerocopy::byteorder::{ByteOrder, NetworkEndian};

    use super::*;

    const DEFAULT_DST_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const DEFAULT_SRC_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);
    const ETHERNET_ETHERTYPE_BYTE_OFFSET: usize = 12;
    const ETHERNET_MIN_FRAME_LEN: usize = 60;

    // Return a buffer for testing parsing with values 0..60 except for the
    // EtherType field, which is EtherType::Arp. Also return the contents
    // of the body.
    fn new_parse_buf() -> ([u8; ETHERNET_MIN_FRAME_LEN], [u8; ETHERNET_MIN_BODY_LEN_NO_TAG]) {
        let mut buf = [0; ETHERNET_MIN_FRAME_LEN];
        for (i, elem) in buf.iter_mut().enumerate() {
            *elem = i as u8;
        }
        NetworkEndian::write_u16(&mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..], EtherType::Arp.into());
        let mut body = [0; ETHERNET_MIN_BODY_LEN_NO_TAG];
        (&mut body).copy_from_slice(&buf[ETHERNET_HDR_LEN_NO_TAG..]);
        (buf, body)
    }

    // Return a test buffer with values 0..46 to be used as a test payload for
    // serialization.
    fn new_serialize_buf() -> [u8; ETHERNET_MIN_BODY_LEN_NO_TAG] {
        let mut buf = [0; ETHERNET_MIN_BODY_LEN_NO_TAG];
        for (i, elem) in buf.iter_mut().enumerate() {
            *elem = i as u8;
        }
        buf
    }

    #[test]
    fn test_parse() {
        crate::testutil::set_logger_for_test();
        let (mut backing_buf, body) = new_parse_buf();
        let mut buf = &mut backing_buf[..];
        // Test parsing with a sufficiently long body.
        let frame = buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
        assert_eq!(frame.hdr_prefix.dst_mac, DEFAULT_DST_MAC);
        assert_eq!(frame.hdr_prefix.src_mac, DEFAULT_SRC_MAC);
        assert!(frame.tag.is_none());
        assert_eq!(frame.ethertype(), Some(EtherType::Arp));
        assert_eq!(frame.body(), &body[..]);
        // Test parsing with a too-short body but length checking disabled.
        let mut buf = &mut backing_buf[..ETHERNET_HDR_LEN_NO_TAG];
        let frame =
            buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::NoCheck).unwrap();
        assert_eq!(frame.hdr_prefix.dst_mac, DEFAULT_DST_MAC);
        assert_eq!(frame.hdr_prefix.src_mac, DEFAULT_SRC_MAC);
        assert!(frame.tag.is_none());
        assert_eq!(frame.ethertype(), Some(EtherType::Arp));
        assert_eq!(frame.body(), &[]);

        // For both of the TPIDs that imply the existence of a tag, make sure
        // that the tag is present and correct (and that all of the normal
        // checks succeed).
        for tpid in [TPID_8021Q, TPID_8021AD].iter() {
            let (mut buf, body) = new_parse_buf();
            let mut buf = &mut buf[..];

            const TPID_OFFSET: usize = 12;
            NetworkEndian::write_u16(&mut buf[TPID_OFFSET..], *tpid);
            // write a valid EtherType
            NetworkEndian::write_u16(&mut buf[TPID_OFFSET + 4..], EtherType::Arp.into());

            let frame =
                buf.parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check).unwrap();
            assert_eq!(frame.hdr_prefix.dst_mac, DEFAULT_DST_MAC);
            assert_eq!(frame.hdr_prefix.src_mac, DEFAULT_SRC_MAC);
            assert_eq!(frame.ethertype(), Some(EtherType::Arp));

            // help out with type inference
            let tag: &U32 = frame.tag.as_ref().unwrap();
            let want_tag =
                u32::from(*tpid) << 16 | ((TPID_OFFSET as u32 + 2) << 8) | (TPID_OFFSET as u32 + 3);
            assert_eq!(tag.get(), want_tag);
            // Offset by 4 since new_parse_buf returns a body on the assumption
            // that there's no tag.
            assert_eq!(frame.body(), &body[4..]);
        }
    }

    #[test]
    fn test_ethertype() {
        // EtherTypes of 1500 and below must match the body length
        let mut buf = [0u8; 1014];
        // an incorrect length results in error
        NetworkEndian::write_u16(&mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..], 1001);
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
            .is_err());

        // a correct length results in success
        NetworkEndian::write_u16(&mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..], 1000);
        assert_eq!(
            (&mut buf[..])
                .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
                .unwrap()
                .ethertype(),
            None
        );

        // an unrecognized EtherType is returned numerically
        let mut buf = [0u8; 1014];
        NetworkEndian::write_u16(
            &mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..],
            ETHERNET_MAX_ILLEGAL_ETHERTYPE + 1,
        );
        assert_eq!(
            (&mut buf[..])
                .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
                .unwrap()
                .ethertype(),
            Some(EtherType::Other(ETHERNET_MAX_ILLEGAL_ETHERTYPE + 1))
        );
    }

    #[test]
    fn test_serialize() {
        let buf = (&new_serialize_buf()[..])
            .into_serializer()
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_DST_MAC,
                DEFAULT_SRC_MAC,
                EtherType::Arp,
            ))
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(
            &buf.as_ref()[..ETHERNET_HDR_LEN_NO_TAG],
            [6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 0x08, 0x06]
        );
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that EthernetFrame::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; ETHERNET_MIN_FRAME_LEN];
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_0[..], ETHERNET_HDR_LEN_NO_TAG..)
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_SRC_MAC,
                DEFAULT_DST_MAC,
                EtherType::Arp,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        let mut buf_1 = [0; ETHERNET_MIN_FRAME_LEN];
        (&mut buf_1[..ETHERNET_HDR_LEN_NO_TAG]).copy_from_slice(&[0xFF; ETHERNET_HDR_LEN_NO_TAG]);
        let _: Buf<&mut [u8]> = Buf::new(&mut buf_1[..], ETHERNET_HDR_LEN_NO_TAG..)
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_SRC_MAC,
                DEFAULT_DST_MAC,
                EtherType::Arp,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_a();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    fn test_parse_error() {
        // 1 byte shorter than the minimum
        let mut buf = [0u8; ETHERNET_MIN_FRAME_LEN - 1];
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
            .is_err());

        // 1 byte shorter than the minimum header length still fails even if
        // length checking is disabled
        let mut buf = [0u8; ETHERNET_HDR_LEN_NO_TAG - 1];
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::NoCheck)
            .is_err());

        // an ethertype of 1500 should be validated as the length of the body
        let mut buf = [0u8; ETHERNET_MIN_FRAME_LEN];
        NetworkEndian::write_u16(
            &mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..],
            ETHERNET_MIN_ILLEGAL_ETHERTYPE - 1,
        );
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
            .is_err());

        // an ethertype of 1501 is illegal because it's in the range [1501, 1535]
        let mut buf = [0u8; ETHERNET_MIN_FRAME_LEN];
        NetworkEndian::write_u16(
            &mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..],
            ETHERNET_MIN_ILLEGAL_ETHERTYPE,
        );
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
            .is_err());

        // an ethertype of 1535 is illegal
        let mut buf = [0u8; ETHERNET_MIN_FRAME_LEN];
        NetworkEndian::write_u16(
            &mut buf[ETHERNET_ETHERTYPE_BYTE_OFFSET..],
            ETHERNET_MAX_ILLEGAL_ETHERTYPE,
        );
        assert!((&mut buf[..])
            .parse_with::<_, EthernetFrame<_>>(EthernetFrameLengthCheck::Check)
            .is_err());
    }

    #[test]
    #[should_panic(expected = "bytes is below minimum frame size of")]
    fn test_serialize_panic() {
        // create with a body which is below the minimum length
        let mut buf = [0u8; ETHERNET_MIN_FRAME_LEN - 1];
        let mut b = [&mut buf[..]];
        let buf = b.as_fragmented_byte_slice();
        let (head, body, foot) = buf.try_split_contiguous(ETHERNET_HDR_LEN_NO_TAG..).unwrap();
        let mut buffer = SerializeBuffer::new(head, body, foot);
        EthernetFrameBuilder::new(
            Mac::new([0, 1, 2, 3, 4, 5]),
            Mac::new([6, 7, 8, 9, 10, 11]),
            EtherType::Arp,
        )
        .serialize(&mut buffer);
    }
}
