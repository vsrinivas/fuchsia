// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod kde;

use crate::Error;
use nom::IResult;
use nom::{call, complete, many0, named, take, try_parse, Needed};
use wlan_common::ie::{rsn::rsne, wpa, Id};

#[derive(Debug, PartialEq)]
pub enum Element {
    Gtk(kde::Header, kde::Gtk),
    Rsne(rsne::Rsne),
    LegacyWpa1(wpa::WpaIe),
    Padding,
    UnsupportedKde(kde::Header),
    UnsupportedIe(u8, u8),
}

fn peek_u8_at(input: &[u8], index: usize) -> IResult<&[u8], u8> {
    if input.len() <= index {
        Err(nom::Err::Incomplete(Needed::Size(index + 1)))
    } else {
        Ok((input, input[index]))
    }
}

fn parse_ie(i0: &[u8]) -> IResult<&[u8], Element> {
    let (i1, id) = try_parse!(i0, call!(peek_u8_at, 0));
    let (i2, len) = try_parse!(i1, call!(peek_u8_at, 1));
    let (out, bytes) = try_parse!(i2, take!(2 + (len as usize)));
    match Id(id) {
        Id::RSNE => {
            let (_, rsne) = try_parse!(bytes, rsne::from_bytes);
            Ok((out, Element::Rsne(rsne)))
        }
        _ => Ok((out, Element::UnsupportedIe(id, len))),
    }
}

fn parse_element(input: &[u8]) -> IResult<&[u8], Element> {
    let (_, type_) = try_parse!(input, call!(peek_u8_at, 0));
    match type_ {
        kde::TYPE => kde::parse(input),
        _ => parse_ie(input),
    }
}

named!(parse_elements<&[u8], Vec<Element>>, many0!(complete!(parse_element)));

pub fn extract_elements(key_data: &[u8]) -> Result<Vec<Element>, Error> {
    match parse_elements(&key_data[..]) {
        Ok((_, elements)) => Ok(elements),
        Err(nom::Err::Error((_, kind))) => Err(Error::InvalidKeyData(kind).into()),
        Err(nom::Err::Failure((_, kind))) => Err(Error::InvalidKeyData(kind).into()),
        Err(nom::Err::Incomplete(_)) => Ok(vec![]),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use wlan_common::{
        assert_variant,
        ie::rsn::{akm, cipher},
        organization::Oui,
    };

    #[test]
    fn test_complex_key_data() {
        #[rustfmt::skip]
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
                    assert_eq!(hdr.oui, Oui::DOT11);
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
                    other => panic!("unexpected IE position: {}", other),
                },
                Element::Rsne(rsne) => match pos {
                    2 => {
                        assert_eq!(rsne.len(), 8);
                        assert_eq!(rsne.version, 257);
                        assert!(rsne.group_data_cipher_suite.is_some());
                        let cipher = rsne.group_data_cipher_suite.unwrap();
                        assert_eq!(cipher.suite_type, 4);
                        let oui = Oui::new([1, 2, 3]);
                        assert_eq!(cipher.oui, oui);
                    }
                    4 => {
                        assert_eq!(rsne.len(), 8);
                        assert_eq!(rsne.version, 9);
                        assert!(rsne.group_data_cipher_suite.is_some());
                        let cipher = rsne.group_data_cipher_suite.unwrap();
                        assert_eq!(cipher.suite_type, 1);
                        assert_eq!(cipher.oui, Oui::DOT11);
                    }
                    other => panic!("unexpected IE position: {}", other),
                },
                Element::UnsupportedKde(hdr) => {
                    assert_eq!(pos, 3);
                    assert_eq!(hdr.type_, 0xDD);
                    assert_eq!(hdr.len, 14);
                    let oui = Oui::new([0x01, 0x0F, 0xAC]);
                    assert_eq!(hdr.oui, oui);
                    assert_eq!(hdr.data_type, 1);
                }
                Element::Padding => assert_eq!(pos, 6),
                _ => panic!("Unexpected element in key data"),
            }
            pos += 1;
        }
    }

    #[test]
    fn test_no_padding() {
        #[rustfmt::skip]
        let buf = [
            10, 14, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, // Unsupported IE
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 1);
        assert_eq!(elements.into_iter().next(), Some(Element::UnsupportedIe(10, 14)));
    }

    #[test]
    fn test_single_padding_byte() {
        #[rustfmt::skip]
        let buf = [
            10, 13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, // Unsupported IE
            0xDD, // 1 byte padding
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 2);

        for e in elements {
            assert_variant!(e, Element::UnsupportedIe(10, 13) | Element::Padding);
        }
    }

    #[test]
    fn test_long_padding() {
        #[rustfmt::skip]
        let buf = [
            20, 6, 1, 2, 3, 4, 5, 6, // Unsupported IE
            0xdd, 0, 0, 0, 0, 0, 0, 0, // 8 bytes padding
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 2);

        for e in elements {
            assert_variant!(e, Element::UnsupportedIe(20, 6) | Element::Padding);
        }
    }

    #[test]
    fn test_gtk() {
        #[rustfmt::skip]
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
            assert_variant!(e, Element::Gtk(hdr, kde) => {
                assert_eq!(
                    hdr,
                    kde::Header { type_: 0xDD, len: 14, oui: Oui::DOT11, data_type: 1 }
                );
                assert_eq!(kde.info.value(), 5);
                assert_eq!(kde.gtk, vec![1, 2, 3, 4, 5, 6, 7, 8]);
            });
        }
    }

    #[test]
    fn test_long_gtk() {
        #[rustfmt::skip]
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
            assert_variant!(e, Element::Gtk(hdr, kde) => {
                assert_eq!(
                    hdr,
                    kde::Header { type_: 0xDD, len: 22, oui: Oui::DOT11, data_type: 1 }
                );
                assert_eq!(kde.info.value(), 200);
                assert_eq!(
                    kde.gtk,
                    vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
                );
            });
        }
    }

    #[test]
    fn test_parse_legacy_wpa() {
        #[rustfmt::skip]
        let buf = [
            // MSFT Vendor IE
            0xdd, 0x16, 0x00, 0x50, 0xf2,
            // WPA header
            0x01, 0x01, 0x00,
            // Multicast cipher
            0x00, 0x50, 0xf2, 0x02,
            // Unicast cipher list
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02,
            // AKM list
            0x01, 0x00, 0x00, 0x50, 0xf2, 0x02,
        ];
        let result = extract_elements(&buf[..]);
        assert!(result.is_ok(), "Error: {:?}", result);

        let elements = result.unwrap();
        assert_eq!(elements.len(), 1);

        for e in elements {
            assert_variant!(e, Element::LegacyWpa1(wpa_ie) => {
                assert_eq!(
                    wpa_ie.multicast_cipher,
                    cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }
                );
                assert_eq!(
                    wpa_ie.unicast_cipher_list,
                    vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }]
                );
                assert_eq!(
                    wpa_ie.akm_list,
                    vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }]
                );
            });
        }
    }
}
