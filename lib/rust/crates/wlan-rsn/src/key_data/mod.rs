// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod kde;

use bytes::{BufMut, BytesMut};
use nom::IResult::{Done, Incomplete};
use nom::{IResult, Needed};
use rsne;
use {Error, Result};

#[derive(Debug)]
pub enum Element {
    Gtk(kde::Header, kde::Gtk),
    Rsne(rsne::Rsne),
    Padding,
    UnsupportedKde(kde::Header),
    UnsupportedIe(u8, u8),
}

fn peek_u8_at<'a>(input: &'a [u8], index: usize) -> IResult<&'a [u8], u8> {
    if input.len() <= index {
        Incomplete(Needed::Size(index))
    } else {
        Done(input, input[index])
    }
}

fn parse_ie<'a>(i0: &'a [u8]) -> IResult<&'a [u8], Element> {
    let (i1, id) = try_parse!(i0, call!(peek_u8_at, 0));
    let (i2, len) = try_parse!(i1, call!(peek_u8_at, 1));
    let (out, bytes) = try_parse!(i2, take!(2 + (len as usize)));
    match id {
        rsne::ID => {
            let (_, rsne) = try_parse!(bytes, rsne::from_bytes);
            Done(out, Element::Rsne(rsne))
        }
        _ => Done(out, Element::UnsupportedIe(id, len)),
    }
}

fn parse_element<'a>(input: &'a [u8]) -> IResult<&'a [u8], Element> {
    let (_, type_) = try_parse!(input, call!(peek_u8_at, 0));
    match type_ {
        kde::TYPE => kde::parse(input),
        _ => parse_ie(input),
    }
}

named!(parse_elements<&[u8], Vec<Element>>, many0!(parse_element));

pub fn extract_elements(key_data: &[u8]) -> Result<Vec<Element>> {
    // Key Data field must be at least 16 bytes long and its length a multiple of 8.
    if key_data.len() % 8 != 0 || key_data.len() < 16 {
        Err(Error::InvaidKeyDataLength(key_data.len()))
    } else {
        parse_elements(key_data)
            .to_full_result()
            .map_err(|e| Error::InvalidKeyData(e))
    }
}

// IEEE Std 802.11-2016, 12.7.2 j)
// Adds padding to a given key data if necessary and truncates all remaining bytes of the buffer.
pub fn add_padding(buf: &mut BytesMut) {
    let padding_len = if buf.len() < 16 {
        16 - buf.len()
    } else {
        ((buf.len() + 7) / 8) * 8 - buf.len()
    };

    if padding_len != 0 {
        // Buffer too small to hold padding; grow buffer.
        if buf.remaining_mut() < padding_len {
            let remaining = buf.remaining_mut();
            buf.reserve(padding_len - remaining);
        }

        buf.put_u8(kde::TYPE);
        buf.put(&vec![0u8; padding_len - 1][..]);
    }

    let written_bytes = buf.len();
    buf.truncate(written_bytes);
}

#[cfg(test)]
mod tests {
    use super::*;

    pub fn is_zero(slice: &[u8]) -> bool {
        slice.iter().all(|&x| x == 0)
    }

    #[test]
    fn test_add_padding_min_length() {
        let mut buf = BytesMut::from(vec![]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 16);
        assert_eq!(buf[0], 0xDD);
        assert!(is_zero(&buf[1..]));

        let mut buf = BytesMut::from(vec![0xFF]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 16);
        assert_eq!(buf[1], 0xDD);
        assert!(is_zero(&buf[2..]));

        // Although length is a multiple of 8, the minimum length should be 16.
        let mut buf = BytesMut::from(vec![0xFF; 8]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 16);
        assert_eq!(buf[8], 0xDD);
        assert!(is_zero(&buf[9..]));

        let mut buf = BytesMut::from(vec![0xFF; 14]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 16);
        assert_eq!(buf[14], 0xDD);
        assert!(is_zero(&buf[15..]));

        let mut buf = BytesMut::from(vec![0xFF; 16]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 16);
        assert_eq!(buf[15], 0xFF);
    }

    #[test]
    fn test_add_padding_8_multiple_length() {
        let mut buf = BytesMut::from(vec![0xFF; 17]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 24);
        assert_eq!(buf[17], 0xDD);
        assert!(is_zero(&buf[18..]));

        let mut buf = BytesMut::from(vec![0xFF; 22]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 24);
        assert_eq!(buf[22], 0xDD);
        assert!(is_zero(&buf[23..]));

        let mut buf = BytesMut::from(vec![0xFF; 24]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 24);
        assert_eq!(buf[23], 0xFF);

        let mut buf = BytesMut::from(vec![0xFF; 25]);
        add_padding(&mut buf);
        assert_eq!(buf.len(), 32);
        assert_eq!(buf[25], 0xDD);
        assert!(is_zero(&buf[26..]));
    }

    #[test]
    fn test_complex_key_data() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            // GTK KDE
            0xDD,
            14, // Length
            0x00, 0x0F, 0xAC, // OUI
            1, // Data Type
            5, // GTK Info
            0, // Reserved
            1, 2, 3, 4, 5, 6, 7, 8, // GTK (8 bytes)
            // Unsupported IE
            99, 6, 1, 2, 3, 4, 5, 6,
            // 1st RSN Element
            48, 6, // IE Header
            1, 1, // Version
            1, 2, 3, 4, // Group Data Cipher
            // Unsupported KDE (wrong OUI)
            0xDD, 14, 0x01, 0x0F, 0xAC,
            1, // Data Type
            5, 0, 1, 2, 3, 4, 5, 6, 7, 8,
            // 2nd RSN Element
            48, 6, // IE Header
            9, 0, // Version
            0x00, 0x0F, 0xAC, 1, // Group Data Cipher
            // Unsupported IE
            200, 2, 1, 3,
            // 4 bytes padding
            0xDD, 0, 0, 0,
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 7);

        let mut pos = 0;
        for e in elements {
            match e {
                Element::Gtk(hdr, kde) => {
                    assert_eq!(pos, 0);
                    assert_eq!(hdr.type_, 0xDD);
                    assert_eq!(hdr.len, 14);
                    assert_eq!(hdr.oui, kde::OUI);
                    assert_eq!(hdr.data_type, 1);
                    assert_eq!(kde.info.value(), 5);
                    assert_eq!(kde.gtk, vec![1, 2, 3, 4, 5, 6, 7, 8]);
                }
                Element::UnsupportedIe(id, len) => match pos {
                    1 => {
                        assert_eq!(id, 99);
                        assert_eq!(len, 6);
                    }
                    5 => {
                        assert_eq!(id, 200);
                        assert_eq!(len, 2);
                    }
                    _ => assert!(false),
                },
                Element::Rsne(rsne) => match pos {
                    2 => {
                        assert_eq!(rsne.len(), 8);
                        assert_eq!(rsne.version, 257);
                        assert!(rsne.group_data_cipher_suite.is_some());
                        let cipher = rsne.group_data_cipher_suite.unwrap();
                        assert_eq!(cipher.suite_type, 4);
                        let oui = vec![1, 2, 3];
                        assert_eq!(cipher.oui, &oui[..]);
                    }
                    4 => {
                        assert_eq!(rsne.len(), 8);
                        assert_eq!(rsne.version, 9);
                        assert!(rsne.group_data_cipher_suite.is_some());
                        let cipher = rsne.group_data_cipher_suite.unwrap();
                        assert_eq!(cipher.suite_type, 1);
                        let oui = vec![0x00, 0x0F, 0xAC];
                        assert_eq!(cipher.oui, &oui[..]);
                    }
                    _ => assert!(false),
                },
                Element::UnsupportedKde(hdr) => {
                    assert_eq!(pos, 3);
                    assert_eq!(hdr.type_, 0xDD);
                    assert_eq!(hdr.len, 14);
                    let oui = vec![0x01, 0x0F, 0xAC];
                    assert_eq!(hdr.oui, &oui[..]);
                    assert_eq!(hdr.data_type, 1);
                }
                Element::Padding => assert_eq!(pos, 6),
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
            pos += 1;
        }
    }

    #[test]
    fn test_too_short_key_data() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            10, 5, 1, 2, 3, 4, 5, // Unsupported IE
        ];
        let result = extract_elements(&buf[..]);
        assert_eq!(result.is_ok(), false, "Error: {:?}", result);
    }

    #[test]
    fn test_not_multiple_of_8() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            // Unsupported IE
            10, 21, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        ];
        let result = extract_elements(&buf[..]);
        assert_eq!(result.is_ok(), false, "Error: {:?}", result);
    }

    #[test]
    fn test_no_padding() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            10, 14, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, // Unsupported IE
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 1);

        for e in elements {
            match e {
                Element::UnsupportedIe(id, len) => {
                    assert_eq!(id, 10);
                    assert_eq!(len, 14);
                }
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
        }
    }

    #[test]
    fn test_single_padding_byte() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            10, 13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, // Unsupported IE
            0xDD, // 1 byte padding
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 2);

        for e in elements {
            match e {
                Element::UnsupportedIe(id, len) => {
                    assert_eq!(id, 10);
                    assert_eq!(len, 13);
                }
                Element::Padding => (),
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
        }
    }

    #[test]
    fn test_long_padding() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            20, 6, 1, 2, 3, 4, 5, 6, // Unsupported IE
            0xdd, 0, 0, 0, 0, 0, 0, 0, // 8 bytes padding
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 2);

        for e in elements {
            match e {
                Element::UnsupportedIe(id, len) => {
                    assert_eq!(id, 20);
                    assert_eq!(len, 6);
                }
                Element::Padding => (),
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
        }
    }

    #[test]
    fn test_gtk() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            // GTK KDE
            0xDD,
            14, // Length
            0x00, 0x0F, 0xAC, // OUI
            1, // Data Type
            5, // GTK Info
            0, // Reserved
            1, 2, 3, 4, 5, 6, 7, 8, // GTK (8 bytes)
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 1);

        for e in elements {
            match e {
                Element::Gtk(hdr, kde) => {
                    assert_eq!(hdr.type_, 0xDD);
                    assert_eq!(hdr.len, 14);
                    assert_eq!(hdr.oui, kde::OUI);
                    assert_eq!(hdr.data_type, 1);
                    assert_eq!(kde.info.value(), 5);
                    assert_eq!(kde.gtk, vec![1, 2, 3, 4, 5, 6, 7, 8]);
                }
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
        }
    }

    #[test]
    fn test_long_gtk() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let buf = [
            // GTK KDE
            0xDD,
            22, // Length
            0x00, 0x0F, 0xAC, // OUI
            1, // Data Type
            200, // GTK Info
            0, // Reserved
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, // GTK (16 bytes)
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 1);

        for e in elements {
            match e {
                Element::Gtk(hdr, kde) => {
                    assert_eq!(hdr.type_, 0xDD);
                    assert_eq!(hdr.len, 22);
                    assert_eq!(hdr.oui, kde::OUI);
                    assert_eq!(hdr.data_type, 1);
                    assert_eq!(kde.info.value(), 200);
                    assert_eq!(
                        kde.gtk,
                        vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
                    );
                }
                _ => assert!(false, "Unexpected element found: {:?}", e),
            }
        }
    }
}
