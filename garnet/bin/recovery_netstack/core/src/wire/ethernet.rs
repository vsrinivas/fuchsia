// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Parsing and serialization of Ethernet frames.

use byteorder::{ByteOrder, NetworkEndian};
use packet::{
    BufferView, BufferViewMut, PacketBuilder, ParsablePacket, ParseMetadata, SerializeBuffer,
};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use crate::device::ethernet::{EtherType, Mac};
use crate::error::ParseError;

// HeaderPrefix has the same memory layout (thanks to repr(C, packed)) as an
// Ethernet header prefix. Thus, we can simply reinterpret the bytes of the
// Ethernet header prefix as a HeaderPrefix and then safely access its fields.
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
#[repr(C, packed)]
struct HeaderPrefix {
    dst_mac: [u8; 6],
    src_mac: [u8; 6],
}

unsafe impl FromBytes for HeaderPrefix {}
unsafe impl AsBytes for HeaderPrefix {}
unsafe impl Unaligned for HeaderPrefix {}

const TPID_8021Q: u16 = 0x8100;
const TPID_8021AD: u16 = 0x88a8;

/// An Ethernet frame.
///
/// An `EthernetFrame` shares its underlying memory with the byte slice it was
/// parsed from or serialized to, meaning that no copying or extra allocation is
/// necessary.
pub struct EthernetFrame<B> {
    hdr_prefix: LayoutVerified<B, HeaderPrefix>,
    tag: Option<LayoutVerified<B, [u8; 4]>>,
    ethertype: LayoutVerified<B, [u8; 2]>,
    body: B,
}

impl<B: ByteSlice> ParsablePacket<B, ()> for EthernetFrame<B> {
    type Error = ParseError;

    fn parse_metadata(&self) -> ParseMetadata {
        let header_len = self.hdr_prefix.bytes().len()
            + self.tag.as_ref().map(|tag| tag.bytes().len()).unwrap_or(0)
            + self.ethertype.bytes().len();
        ParseMetadata::from_packet(header_len, self.body.len(), 0)
    }

    fn parse<BV: BufferView<B>>(mut buffer: BV, args: ()) -> Result<Self, ParseError> {
        // See for details: https://en.wikipedia.org/wiki/Ethernet_frame#Frame_%E2%80%93_data_link_layer

        let hdr_prefix = buffer
            .take_obj_front::<HeaderPrefix>()
            .ok_or_else(debug_err_fn!(
                ParseError::Format,
                "too few bytes for header"
            ))?;
        if buffer.len() < 48 {
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
        let ethertype_or_tpid = NetworkEndian::read_u16(buffer.as_ref());
        let (tag, ethertype, body) = match ethertype_or_tpid {
            self::TPID_8021Q | self::TPID_8021AD => (
                Some(buffer.take_obj_front::<[u8; 4]>().unwrap()),
                buffer.take_obj_front::<[u8; 2]>().unwrap(),
                buffer.into_rest(),
            ),
            _ => (
                None,
                buffer.take_obj_front::<[u8; 2]>().unwrap(),
                buffer.into_rest(),
            ),
        };

        let frame = EthernetFrame {
            hdr_prefix,
            tag,
            ethertype,
            body,
        };

        let et = NetworkEndian::read_u16(&*frame.ethertype);
        if (et > 1500 && et < 1536) || (et <= 1500 && et as usize != frame.body.len()) {
            // EtherType values between 1500 and 1536 are disallowed, and values
            // of 1500 and below are used to indicate the body length.
            return debug_err!(
                Err(ParseError::Format),
                "invalid ethertype number: {:x}",
                et
            );
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
        Mac::new(self.hdr_prefix.src_mac)
    }

    /// The destination MAC address.
    pub fn dst_mac(&self) -> Mac {
        Mac::new(self.hdr_prefix.dst_mac)
    }

    /// The EtherType.
    ///
    /// `ethertype` returns the `EtherType` from the Ethernet header. However:
    /// - Some values of the EtherType header field are used to indicate the
    ///   length of the frame's body. In this case, `ethertype` returns `None`.
    /// - If the EtherType number is unrecognized, then `ethertype` returns
    ///   `Ok(Err(x))` where `x` is the numerical EtherType number.
    pub fn ethertype(&self) -> Option<Result<EtherType, u16>> {
        let et = NetworkEndian::read_u16(&self.ethertype[..]);
        if et <= 1500 {
            return None;
        }
        // values in (1500, 1536) are illegal, and shouldn't make it through
        // parse
        debug_assert!(et >= 1536);
        Some(EtherType::from_u16(et).ok_or(et))
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
    fn total_frame_len(&self) -> usize {
        self.header_len() + self.body.len()
    }

    /// Construct a builder with the same contents as this frame.
    pub fn builder(&self) -> EthernetFrameBuilder {
        EthernetFrameBuilder {
            src_mac: self.src_mac(),
            dst_mac: self.dst_mac(),
            ethertype: NetworkEndian::read_u16(&self.ethertype[..]),
        }
    }
}

/// A builder for Ethernet frames.
pub struct EthernetFrameBuilder {
    src_mac: Mac,
    dst_mac: Mac,
    ethertype: u16,
}

impl EthernetFrameBuilder {
    /// Construct a new `EthernetFrameBuilder`.
    pub fn new(src_mac: Mac, dst_mac: Mac, ethertype: EtherType) -> EthernetFrameBuilder {
        EthernetFrameBuilder {
            src_mac,
            dst_mac,
            ethertype: ethertype as u16,
        }
    }
}

// NOTE(joshlf): MIN_BODY_BYTES assumes no 802.1Q or 802.1ad tag. We don't
// support creating new packets with these tags at the moment, so this is a
// reasonable assumption. If we support tags in the future, this minimum will
// only go down, so it is forwards-compatible.

const MAX_HEADER_BYTES: usize = 18;
const MIN_HEADER_BYTES: usize = 14;
const MIN_BODY_BYTES: usize = 46;

impl PacketBuilder for EthernetFrameBuilder {
    fn header_len(&self) -> usize {
        MIN_HEADER_BYTES
    }

    fn min_body_len(&self) -> usize {
        MIN_BODY_BYTES
    }

    fn footer_len(&self) -> usize {
        0
    }

    fn serialize<'a>(self, mut buffer: SerializeBuffer<'a>) {
        // NOTE: EtherType values of 1500 and below are used to indicate the
        // length of the body in bytes. We don't need to validate this because
        // the EtherType enum has no variants with values in that range.

        let (mut header, mut body, _) = buffer.parts();
        // implements BufferViewMut, giving us take_obj_xxx_zero methods
        let mut header = &mut header;

        let mut frame = {
            // SECURITY: Use _zero constructors to ensure we zero memory to
            // prevent leaking information from packets previously stored in
            // this buffer.
            let hdr_prefix = header
                .take_obj_front_zero::<HeaderPrefix>()
                .expect("too few bytes for Ethernet header");
            let ethertype = header
                .take_obj_front_zero::<[u8; 2]>()
                .expect("too few bytes for Ethernet header");
            EthernetFrame {
                hdr_prefix,
                tag: None,
                ethertype,
                body,
            }
        };

        let total_len = frame.total_frame_len();
        if total_len < 60 {
            panic!(
                "total frame size of {} bytes is below minimum frame size of 60",
                total_len
            );
        }

        frame.hdr_prefix.src_mac = self.src_mac.bytes();
        frame.hdr_prefix.dst_mac = self.dst_mac.bytes();
        NetworkEndian::write_u16(&mut frame.ethertype[..], self.ethertype);
    }
}

#[cfg(test)]
mod tests {
    use packet::{Buf, BufferSerializer, ParseBuffer, SerializeBuffer, Serializer};

    use super::*;

    const DEFAULT_DST_MAC: Mac = Mac::new([0, 1, 2, 3, 4, 5]);
    const DEFAULT_SRC_MAC: Mac = Mac::new([6, 7, 8, 9, 10, 11]);

    // Return a buffer for testing parsing with values 0..60 except for the
    // EtherType field, which is EtherType::Arp. Also return the contents
    // of the body.
    fn new_parse_buf() -> ([u8; 60], [u8; 46]) {
        let mut buf = [0; 60];
        for i in 0..60 {
            buf[i] = i as u8;
        }
        NetworkEndian::write_u16(&mut buf[12..14], EtherType::Arp as u16);
        let mut body = [0; 46];
        (&mut body).copy_from_slice(&buf[14..]);
        (buf, body)
    }

    // Return a test buffer with values 0..46 to be used as a test payload for
    // serialization.
    fn new_serialize_buf() -> [u8; 46] {
        let mut buf = [0; 46];
        for i in 0..46 {
            buf[i] = i as u8;
        }
        buf
    }

    #[test]
    fn test_parse() {
        let (mut buf, body) = new_parse_buf();
        let mut buf = &mut buf[..];
        let frame = buf.parse::<EthernetFrame<_>>().unwrap();
        assert_eq!(frame.hdr_prefix.dst_mac, DEFAULT_DST_MAC.bytes());
        assert_eq!(frame.hdr_prefix.src_mac, DEFAULT_SRC_MAC.bytes());
        assert!(frame.tag.is_none());
        assert_eq!(frame.ethertype(), Some(Ok(EtherType::Arp)));
        assert_eq!(frame.body(), &body[..]);

        // For both of the TPIDs that imply the existence of a tag, make sure
        // that the tag is present and correct (and that all of the normal
        // checks succeed).
        for tpid in [TPID_8021Q, TPID_8021AD].iter() {
            let (mut buf, body) = new_parse_buf();
            let mut buf = &mut buf[..];

            const TPID_OFFSET: usize = 12;
            NetworkEndian::write_u16(&mut buf[TPID_OFFSET..], *tpid);
            // write a valid EtherType
            NetworkEndian::write_u16(&mut buf[TPID_OFFSET + 4..], EtherType::Arp as u16);

            let frame = buf.parse::<EthernetFrame<_>>().unwrap();
            assert_eq!(frame.hdr_prefix.dst_mac, DEFAULT_DST_MAC.bytes());
            assert_eq!(frame.hdr_prefix.src_mac, DEFAULT_SRC_MAC.bytes());
            assert_eq!(frame.ethertype(), Some(Ok(EtherType::Arp)));

            // help out with type inference
            let tag: &[u8; 4] = frame.tag.as_ref().unwrap();
            let got_tag = NetworkEndian::read_u32(tag);
            let want_tag =
                (*tpid as u32) << 16 | ((TPID_OFFSET as u32 + 2) << 8) | (TPID_OFFSET as u32 + 3);
            assert_eq!(got_tag, want_tag);
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
        NetworkEndian::write_u16(&mut buf[12..], 1001);
        assert!((&mut buf[..]).parse::<EthernetFrame<_>>().is_err());

        // a correct length results in success
        NetworkEndian::write_u16(&mut buf[12..], 1000);
        assert_eq!(
            (&mut buf[..])
                .parse::<EthernetFrame<_>>()
                .unwrap()
                .ethertype(),
            None
        );

        // an unrecognized EtherType is returned numerically
        let mut buf = [0u8; 1014];
        NetworkEndian::write_u16(&mut buf[12..], 1536);
        assert_eq!(
            (&mut buf[..])
                .parse::<EthernetFrame<_>>()
                .unwrap()
                .ethertype(),
            Some(Err(1536))
        );
    }

    #[test]
    fn test_serialize() {
        let buf = (&new_serialize_buf()[..])
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_DST_MAC,
                DEFAULT_SRC_MAC,
                EtherType::Arp,
            ))
            .serialize_outer();
        assert_eq!(
            &buf.as_ref()[..MAX_HEADER_BYTES - 4],
            [6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 0x08, 0x06]
        );
    }

    #[test]
    fn test_serialize_zeroes() {
        // Test that EthernetFrame::serialize properly zeroes memory before
        // serializing the header.
        let mut buf_0 = [0; 60];

        BufferSerializer::new_vec(Buf::new(&mut buf_0[..], 14..))
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_SRC_MAC,
                DEFAULT_DST_MAC,
                EtherType::Arp,
            ))
            .serialize_outer();
        let mut buf_1 = [0; 60];
        (&mut buf_1[..14]).copy_from_slice(&[0xFF; 14]);
        BufferSerializer::new_vec(Buf::new(&mut buf_1[..], 14..))
            .encapsulate(EthernetFrameBuilder::new(
                DEFAULT_SRC_MAC,
                DEFAULT_DST_MAC,
                EtherType::Arp,
            ))
            .serialize_outer();
        assert_eq!(&buf_0[..], &buf_1[..]);
    }

    #[test]
    fn test_parse_error() {
        // 1 byte shorter than the minimum
        let mut buf = [0u8; 59];
        assert!((&mut buf[..]).parse::<EthernetFrame<_>>().is_err());

        // an ethertype of 1500 should be validated as the length of the body
        let mut buf = [0u8; 60];
        NetworkEndian::write_u16(&mut buf[12..], 1500);
        assert!((&mut buf[..]).parse::<EthernetFrame<_>>().is_err());

        // an ethertype of 1501 is illegal because it's in the range [1501, 1535]
        let mut buf = [0u8; 60];
        NetworkEndian::write_u16(&mut buf[12..], 1501);
        assert!((&mut buf[..]).parse::<EthernetFrame<_>>().is_err());

        // an ethertype of 1535 is illegal
        let mut buf = [0u8; 60];
        NetworkEndian::write_u16(&mut buf[12..], 1535);
        assert!((&mut buf[..]).parse::<EthernetFrame<_>>().is_err());
    }

    #[test]
    #[should_panic]
    fn test_serialize_panic() {
        // create with a body which is below the minimum length
        let mut buf = [0u8; 60];
        let buffer = SerializeBuffer::new(&mut buf[..], (60 - (MIN_BODY_BYTES - 1))..);
        EthernetFrameBuilder::new(
            Mac::new([0, 1, 2, 3, 4, 5]),
            Mac::new([6, 7, 8, 9, 10, 11]),
            EtherType::Arp,
        )
        .serialize(buffer);
    }
}
