// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::rsn::{akm, cipher, suite_selector};

use crate::appendable::{Appendable, BufferTooSmall};
use crate::organization::Oui;
use nom::number::streaming::le_u16;
use nom::{call, count, do_parse, eof, named, named_attr, take, try_parse, IResult};

// The WPA1 IE is not fully specified by IEEE. This format was derived from pcap.
// Note that this file only parses fields specific to WPA -- IE headers and MSFT-specific fields
// are omitted.
// (3B) OUI
pub const OUI: Oui = Oui::MSFT;
// (1B) OUI-specific element type
pub const VENDOR_SPECIFIC_TYPE: u8 = 1;
// (2B) WPA type
pub const WPA_TYPE: u16 = 1;
// (4B) multicast cipher
//     0-2 cipher suite (OUI)
//     3   cipher type
// (2B) unicast cipher count
// (4B x N) unicast cipher list
// (2B) AKM count
// (4B x N) AKM list
#[derive(Debug, PartialOrd, PartialEq, Eq, Clone)]
pub struct WpaIe {
    pub multicast_cipher: cipher::Cipher,
    pub unicast_cipher_list: Vec<cipher::Cipher>,
    pub akm_list: Vec<akm::Akm>,
}

impl WpaIe {
    const FIXED_FIELDS_LENGTH: usize = 10;
    pub fn len(&self) -> usize {
        Self::FIXED_FIELDS_LENGTH + self.unicast_cipher_list.len() * 4 + self.akm_list.len() * 4
    }

    pub fn write_into<A: Appendable>(&self, buf: &mut A) -> Result<(), BufferTooSmall> {
        if !buf.can_append(self.len()) {
            return Err(BufferTooSmall);
        }

        buf.append_value(&WPA_TYPE)?;

        buf.append_bytes(&self.multicast_cipher.oui[..])?;
        buf.append_value(&self.multicast_cipher.suite_type)?;

        buf.append_value(&(self.unicast_cipher_list.len() as u16))?;
        for cipher in &self.unicast_cipher_list {
            buf.append_bytes(&cipher.oui[..])?;
            buf.append_value(&cipher.suite_type)?;
        }

        buf.append_value(&(self.akm_list.len() as u16))?;
        for akm in &self.akm_list {
            buf.append_bytes(&akm.oui[..])?;
            buf.append_value(&akm.suite_type)?;
        }

        Ok(())
    }
}

fn read_suite_selector<T>(input: &[u8]) -> IResult<&[u8], T>
where
    T: suite_selector::Factory<Suite = T>,
{
    let (i1, bytes) = try_parse!(input, take!(4));
    let oui = Oui::new([bytes[0], bytes[1], bytes[2]]);
    return Ok((i1, T::new(oui, bytes[3])));
}

named!(parse_akm<&[u8], akm::Akm>, call!(read_suite_selector::<akm::Akm>));
named!(parse_cipher<&[u8], cipher::Cipher>, call!(read_suite_selector::<cipher::Cipher>));

// Take as many zeroes as possible from the beginning of the buffer. Unlike nom's take_while, this
// handles the case where we run into the end of the buffer.
fn take_while_zero(input: &[u8]) -> IResult<&[u8], ()> {
    for i in 0..input.len() {
        if input[i] != 0 {
            return Ok((&input[i..], ()));
        }
    }
    Ok((&[], ()))
}

named_attr!(
    /// Convert bytes of a WPA information element into a WpaIe representation.
    , // comma ends the attribute list to named_attr
    pub from_bytes<&[u8], WpaIe>,
      do_parse!(
          _wpa_type: le_u16 >>
          multicast_cipher: parse_cipher >>
          unicast_cipher_count: le_u16 >>
          unicast_cipher_list: count!(parse_cipher, unicast_cipher_count as usize) >>
          akm_count: le_u16 >>
          akm_list: count!(parse_akm, akm_count as usize) >>
          // In practice this IE is sometimes zero-padded.
          call!(take_while_zero) >>
          eof!() >>
          (WpaIe{
              multicast_cipher,
              unicast_cipher_list,
              akm_list,
          })
      )
);

#[cfg(test)]
mod tests {
    use super::*;

    #[rustfmt::skip]
    const DEFAULT_FRAME: [u8; 18] = [
        // WPA version
        0x01, 0x00,
        // Multicast cipher
        0x00, 0x50, 0xf2, 0x02,
        // Unicast cipher list
        0x01, 0x00, 0x00, 0x50, 0xf2, 0x02,
        // AKM list
        0x01, 0x00, 0x00, 0x50, 0xf2, 0x02,
    ];

    #[test]
    fn test_write_into() {
        let wpa_frame = WpaIe {
            multicast_cipher: cipher::Cipher { oui: OUI, suite_type: cipher::TKIP },
            unicast_cipher_list: vec![cipher::Cipher { oui: OUI, suite_type: cipher::TKIP }],
            akm_list: vec![akm::Akm { oui: OUI, suite_type: akm::PSK }],
        };

        let mut wpa_frame_bytes = vec![];
        wpa_frame.write_into(&mut wpa_frame_bytes).expect("failed to write frame");
        assert_eq!(&wpa_frame_bytes[..], &DEFAULT_FRAME[..]);
    }

    #[test]
    fn test_write_into_roundtrip() {
        let wpa_frame = from_bytes(&DEFAULT_FRAME[..]);
        assert!(wpa_frame.is_ok());
        let wpa_frame = wpa_frame.unwrap().1;
        let mut wpa_frame_bytes = vec![];
        wpa_frame.write_into(&mut wpa_frame_bytes).expect("failed to write frame");
        assert_eq!(&wpa_frame_bytes[..], &DEFAULT_FRAME[..]);
    }

    #[test]
    fn test_parse_correct() {
        let wpa_frame = from_bytes(&DEFAULT_FRAME[..]);
        assert!(wpa_frame.is_ok());
        let wpa_frame = wpa_frame.unwrap().1;
        assert_eq!(
            wpa_frame.multicast_cipher,
            cipher::Cipher { oui: OUI, suite_type: cipher::TKIP }
        );
        assert_eq!(
            wpa_frame.unicast_cipher_list,
            vec![cipher::Cipher { oui: OUI, suite_type: cipher::TKIP }]
        );
        assert_eq!(wpa_frame.akm_list, vec![akm::Akm { oui: OUI, suite_type: akm::PSK }]);
    }

    #[test]
    fn test_parse_bad_frame() {
        #[rustfmt::skip]
        let bad_frame: Vec<u8> = vec![
            // WPA version
            0x01, 0x00,
            // Multicast cipher
            0x00, 0x50, 0xf2, 0x02,
            // Unicast cipher list (count is incorrect)
            0x16, 0x00, 0x00, 0x50, 0xf2, 0x02,
            // AKM list
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02,
        ];
        let wpa_frame = from_bytes(&bad_frame[..]);
        assert!(!wpa_frame.is_ok());
    }

    #[test]
    fn test_truncated_frame() {
        #[rustfmt::skip]
        let bad_frame: Vec<u8> = vec![
            // WPA version
            0x01, 0x00,
            // Multicast ciph... truncated frame.
            0x00, 0x50
        ];
        let wpa_frame = from_bytes(&bad_frame[..]);
        assert!(!wpa_frame.is_ok());
    }

    #[test]
    fn test_parse_with_padding() {
        let mut frame = DEFAULT_FRAME.to_vec();
        frame.resize(DEFAULT_FRAME.len() + 5, 0);

        let wpa_frame = from_bytes(&frame[..]);
        assert!(wpa_frame.is_ok());
        let wpa_frame = wpa_frame.unwrap().1;
        assert_eq!(
            wpa_frame.multicast_cipher,
            cipher::Cipher { oui: OUI, suite_type: cipher::TKIP }
        );
        assert_eq!(
            wpa_frame.unicast_cipher_list,
            vec![cipher::Cipher { oui: OUI, suite_type: cipher::TKIP }]
        );
        assert_eq!(wpa_frame.akm_list, vec![akm::Akm { oui: OUI, suite_type: akm::PSK }]);

        frame[DEFAULT_FRAME.len() + 1] = 1;
        let wpa_frame = from_bytes(&frame[..]);
        assert!(!wpa_frame.is_ok());
    }
}
