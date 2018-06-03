// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::RangeBounds;

use byteorder::{BigEndian, ByteOrder};
use zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned};

use device::ethernet::{EtherType, Mac};
use wire::util::extract_slice_range;
use wire::{Err, ParseErr};

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
//     Instead, we use the BigEndian type and its reader and writer methods to
//     correctly access these fields.
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

/// The maximum length of an Ethernet header in bytes.
///
/// When calling `EthernetFrame::create`, provide at least `MAX_HEADER_LEN`
/// bytes for the header in order to guarantee that `create` will not panic.
pub const MAX_HEADER_LEN: usize = 18;

// NOTE(joshlf): MIN_BODY_LEN assumes no 802.1Q or 802.1ad tag. We don't support
// creating new packets with these tags at the moment, so this is a reasonable
// assumption. If we support tags in the future, this minimum will only go down,
// so it is forwards-compatible.

/// The minimum length of an Ethernet body in bytes.
///
/// When calling `EthernetFrame::create`, provide at least `MIN_BODY_LEN` bytes
/// for the body in order to guarantee that `create` will not panic.
pub const MIN_BODY_LEN: usize = 46;

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

impl<B: ByteSlice> EthernetFrame<B> {
    /// Parse an Ethernet frame.
    ///
    /// `parse` parses `bytes` as an Ethernet frame. It is assumed that the
    /// Frame Check Sequence (FCS) footer has already been removed.
    pub fn parse(bytes: B) -> Result<EthernetFrame<B>, impl ParseErr> {
        // See for details: https://en.wikipedia.org/wiki/Ethernet_frame#Frame_%E2%80%93_data_link_layer

        let (hdr_prefix, rest) =
            LayoutVerified::<B, HeaderPrefix>::new_unaligned_from_prefix(bytes).ok_or(Err::Format)?;
        if rest.len() < 48 {
            // The minimum frame size (not including the Frame Check Sequence
            // (FCS) footer, which we do not handle in this code) is 60 bytes.
            // We've already consumed 12 bytes for the header prefix, so we must
            // have at least 48 bytes left.
            return Err(Err::Format);
        }

        // The tag (either IEEE 802.1Q or 802.1ad) is an optional four-byte
        // field. If present, it precedes the ethertype, and its first two bytes
        // (where the ethertype bytes are normally) are called the Tag Protocol
        // Identifier (TPID). A TPID of TPID_8021Q implies an 802.1Q tag, a TPID
        // of TPID_8021AD implies an 802.1ad tag, and anything else implies that
        // there is no tag - it's a normal ethertype field.
        let ethertype_or_tpid = BigEndian::read_u16(&rest);
        let (tag, ethertype, body) = match ethertype_or_tpid {
            self::TPID_8021Q | self::TPID_8021AD => {
                let (tag, rest) =
                    LayoutVerified::<B, [u8; 4]>::new_unaligned_from_prefix(rest).unwrap();
                let (ethertype, body) =
                    LayoutVerified::<B, [u8; 2]>::new_unaligned_from_prefix(rest).unwrap();
                (Some(tag), ethertype, body)
            }
            _ => {
                let (ethertype, body) =
                    LayoutVerified::<B, [u8; 2]>::new_unaligned_from_prefix(rest).unwrap();
                (None, ethertype, body)
            }
        };

        let frame = EthernetFrame {
            hdr_prefix,
            tag,
            ethertype,
            body,
        };

        let et = frame.ethertype();
        if (et > 1500 && et < 1536) || (et <= 1500 && et as usize != frame.body.len()) {
            // EtherType values between 1500 and 1536 are disallowed, and values
            // of 1500 and below are used to indicate the body size.
            return Err(Err::Format);
        }
        Ok(frame)
    }

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

    /// The numerical EtherType code.
    pub fn ethertype(&self) -> u16 {
        BigEndian::read_u16(&self.ethertype[..])
    }
}

impl<'a> EthernetFrame<&'a mut [u8]> {
    /// Serialize an Ethernet frame in an existing buffer.
    ///
    /// `create` creates an `EthernetFrame` which uses the provided `buffer` for
    /// its storage, initializing all header fields. It treats the range
    /// identified by `body` as being the frame body. It uses the last bytes of
    /// `buffer` before the body to store the header, and returns the number of
    /// remaining prefix bytes to the caller for use in adding other headers.
    ///
    /// # Examples
    ///
    /// ```rust
    /// let mut buffer = [0u8; 1024];
    /// (&mut buffer[512..]).copy_from_slice(body);
    /// let (frame, prefix_bytes) =
    ///     EthernetFrame::create(&mut buffer, 512.., src_mac, dst_mac, ethertype);
    /// ```
    ///
    /// # Panics
    ///
    /// `create` panics if there is insufficient room preceding the body to
    /// store the Ethernet header, or if `body` is not in range of `buffer`. The
    /// caller can guarantee that there will be enough room by providing at
    /// least `MAX_HEADER_LEN` pre-body bytes.
    ///
    /// `create` also panics if the total frame length is less than the minimum
    /// of 60 bytes. The caller can guarantee that the frame will be large
    /// enough by providing a body of at least `MIN_BODY_LEN` bytes.
    pub fn create<R: RangeBounds<usize>>(
        buffer: &'a mut [u8], body: R, src_mac: Mac, dst_mac: Mac, ethertype: EtherType,
    ) -> (EthernetFrame<&'a mut [u8]>, usize) {
        // NOTE: EtherType values of 1500 and below are used to indicate the
        // length of the body in bytes. We don't need to validate this because
        // the EtherType enum has no variants with values in that range.

        let (header, body, _) =
            extract_slice_range(buffer, body).expect("body range is out of bounds of buffer");
        let (mut frame, prefix_len) = {
            let (ethertype, prefix) = LayoutVerified::<_, [u8; 2]>::new_unaligned_from_suffix(
                header,
            ).expect("too few bytes for Ethernet header");
            let (hdr_prefix, prefix) = LayoutVerified::<_, HeaderPrefix>::new_unaligned_from_suffix(
                prefix,
            ).expect("too few bytes for Ethernet header");
            (
                EthernetFrame {
                    hdr_prefix,
                    tag: None,
                    ethertype,
                    body,
                },
                prefix.len(),
            )
        };

        let total_len =
            frame.hdr_prefix.bytes().len() + frame.ethertype.bytes().len() + frame.body.len();
        if total_len < 60 {
            panic!(
                "total frame size of {} bytes is below minimum frame size of 60",
                total_len
            );
        }

        frame.hdr_prefix.src_mac = src_mac.bytes();
        frame.hdr_prefix.dst_mac = dst_mac.bytes();
        BigEndian::write_u16(&mut frame.ethertype[..], ethertype as u16);
        (frame, prefix_len)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // return a test buffer with values 0..60
    fn new_buf() -> [u8; 60] {
        let mut buf = [0u8; 60];
        for i in 0..60 {
            buf[i] = i as u8;
        }
        buf
    }

    #[test]
    fn test_parse() {
        let buf = new_buf();
        let frame = EthernetFrame::parse(&buf[..]).unwrap();
        assert_eq!(frame.hdr_prefix.dst_mac, [0, 1, 2, 3, 4, 5]);
        assert_eq!(frame.hdr_prefix.src_mac, [6, 7, 8, 9, 10, 11]);
        assert!(frame.tag.is_none());
        // help out with type inference
        let ethertype: &[u8; 2] = &frame.ethertype;
        assert_eq!(ethertype, &[12, 13]);
        assert_eq!(frame.body, &buf[14..]);

        // For both of the TPIDs that imply the existence of a tag, make sure
        // that the tag is present and correct (and that all of the normal
        // checks succeed).
        for tpid in [TPID_8021Q, TPID_8021AD].iter() {
            let mut buf = new_buf();

            const TPID_OFFSET: usize = 12;
            BigEndian::write_u16(&mut buf[TPID_OFFSET..], *tpid);

            let frame = EthernetFrame::parse(&buf[..]).unwrap();
            assert_eq!(frame.hdr_prefix.dst_mac, [0, 1, 2, 3, 4, 5]);
            assert_eq!(frame.hdr_prefix.src_mac, [6, 7, 8, 9, 10, 11]);

            // help out with type inference
            let tag: &[u8; 4] = &frame.tag.unwrap();
            let got_tag = BigEndian::read_u32(tag);
            let want_tag =
                (*tpid as u32) << 16 | ((TPID_OFFSET as u32 + 2) << 8) | (TPID_OFFSET as u32 + 3);
            assert_eq!(got_tag, want_tag);
            // help out with type inference
            let ethertype: &[u8; 2] = &frame.ethertype;
            assert_eq!(ethertype, &[16, 17]);
            assert_eq!(frame.body, &buf[18..]);
        }
    }

    #[test]
    fn test_ethertype_body_len() {
        // ethertypes of 1500 and below must match the body length
        let mut buf = [0u8; 1014];
        // an incorrect length results in error
        BigEndian::write_u16(&mut buf[12..], 1001);
        assert!(EthernetFrame::parse(&buf[..]).is_err());
        // a correct length results in success
        BigEndian::write_u16(&mut buf[12..], 1000);
        assert!(EthernetFrame::parse(&buf[..]).is_ok());
    }

    #[test]
    fn test_create() {
        let mut buf = new_buf();
        {
            let (_, prefix_len) = EthernetFrame::create(
                &mut buf,
                (MAX_HEADER_LEN - 4)..,
                Mac::new([0, 1, 2, 3, 4, 5]),
                Mac::new([6, 7, 8, 9, 10, 11]),
                EtherType::Arp,
            );
            assert_eq!(prefix_len, 0);
        }
        assert_eq!(
            &buf[..MAX_HEADER_LEN - 4],
            [6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5, 0x08, 0x06]
        );
    }

    #[test]
    fn test_parse_error() {
        // 1 byte shorter than the minimum
        let buf = [0u8; 59];
        assert!(EthernetFrame::parse(&buf[..]).is_err());

        // an ethertype of 1500 should be validated as the length of the body
        let mut buf = [0u8; 60];
        BigEndian::write_u16(&mut buf[12..], 1500);
        assert!(EthernetFrame::parse(&buf[..]).is_err());

        // an ethertype of 1501 is illegal
        let mut buf = [0u8; 60];
        BigEndian::write_u16(&mut buf[12..], 1501);
        assert!(EthernetFrame::parse(&buf[..]).is_err());

        // an ethertype of 1535 is illegal
        let mut buf = [0u8; 60];
        BigEndian::write_u16(&mut buf[12..], 1535);
        assert!(EthernetFrame::parse(&buf[..]).is_err());
    }

    #[test]
    #[should_panic]
    fn test_create_panic() {
        // create with a body which is below the minimum length
        let mut buf = [0u8; 60];
        EthernetFrame::create(
            &mut buf,
            (60 - (MIN_BODY_LEN - 1))..,
            Mac::new([0, 1, 2, 3, 4, 5]),
            Mac::new([6, 7, 8, 9, 10, 11]),
            EtherType::Arp,
        );
    }
}
