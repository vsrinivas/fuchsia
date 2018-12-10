// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitfield::bitfield,
    zerocopy::{AsBytes, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
const IE_ID_SSID: u8 = 0;
const IE_ID_SUPPORTED_RATES: u8 = 1;
const IE_ID_DSSS_PARAM_SET: u8 = 3;
const IE_ID_TIM: u8 = 5;

// IEEE Std 802.11-2016, 9.4.2.2
const SSID_IE_MIN_BODY_LEN: usize = 0;
const SSID_IE_MAX_BODY_LEN: usize = 32;

// IEEE Std 802.11-2016, 9.4.2.3
const SUPP_RATES_IE_MIN_BODY_LEN: usize = 1;
const SUPP_RATES_IE_MAX_BODY_LEN: usize = 8;

// IEEE Std 802.11-2016, 9.4.2.4
const DSSS_PARAM_SET_IE_BODY_LEN: usize = 1;

// IEEE Std 802.11-2016, 9.4.2.6
const TIM_IE_MIN_PVB_LEN: usize = 1;
const TIM_IE_MAX_PVB_LEN: usize = 251;

// IEEE Std 802.11-2016, 9.4.2.1
#[repr(C, packed)]
pub struct InfoElementHdr {
    pub id: u8,
    pub body_len: u8,
}
// Safe: see macro explanation.
unsafe_impl_zerocopy_traits!(InfoElementHdr);

impl InfoElementHdr {
    pub fn body_len(&self) -> usize {
        self.body_len as usize
    }
}

// IEEE Std 802.11-2016, 9.4.2.3
bitfield! {
    #[derive(PartialEq)]
    pub struct SupportedRate(u8);
    impl Debug;

    pub rate, set_rate: 6, 0;
    pub basic, set_basic: 7;

    pub value, _: 7,0;
}

// IEEE Std 802.11-2016, 9.2.4.6
bitfield! {
    #[derive(PartialEq)]
    pub struct BitmapControl(u8);
    impl Debug;

    pub group_traffic, set_group_traffic: 0;
    pub offset, set_offset: 7, 1;

    pub value, _: 7,0;
}

impl BitmapControl {
    pub fn from_bytes(bytes: &[u8]) -> Option<BitmapControl> {
        if bytes.is_empty() {
            None
        } else {
            Some(BitmapControl(bytes[0]))
        }
    }
}

// IEEE Std 802.11-2016, 9.4.2.6
pub struct Tim<B> {
    pub dtim_count: u8,
    pub dtim_period: u8,
    pub bmp_ctrl: u8,
    pub partial_virtual_bmp: B,
}

pub struct InfoElementReader<'a>(&'a [u8]);

impl<'a> InfoElementReader<'a> {
    pub fn has_remaining(&self) -> bool {
        !self.0.is_empty()
    }

    pub fn remaining(&self) -> &'a [u8] {
        self.0
    }
}

impl<'a> Iterator for InfoElementReader<'a> {
    type Item = InfoElement<&'a [u8]>;

    fn next(&mut self) -> Option<InfoElement<&'a [u8]>> {
        let (item, remaining) = InfoElement::parse(&self.0[..])?;
        self.0 = remaining;
        Some(item)
    }
}

pub enum InfoElement<B> {
    // IEEE Std 802.11-2016, 9.4.2.2
    Ssid(B),
    // IEEE Std 802.11-2016, 9.4.2.3
    SupportedRates(B),
    // IEEE Std 802.11-2016, 9.4.2.4
    DsssParamSet { channel: u8 },
    // IEEE Std 802.11-2016, 9.4.2.6
    Tim(Tim<B>),
    Unsupported { id: u8, body: B },
}

impl<B: ByteSlice> InfoElement<B> {
    pub fn parse(bytes: B) -> Option<(InfoElement<B>, B)> {
        let (hdr, body) = LayoutVerified::<B, InfoElementHdr>::new_unaligned_from_prefix(bytes)?;
        if hdr.body_len() > body.len() {
            return None;
        }
        let (body, remaining) = body.split_at(hdr.body_len());

        match hdr.id {
            // IEEE Std 802.11-2016, 9.4.2.2
            IE_ID_SSID => {
                let ssid = parse_fixed_length_ie(body, SSID_IE_MIN_BODY_LEN, SSID_IE_MAX_BODY_LEN)?;
                Some((InfoElement::Ssid(ssid), remaining))
            }
            // IEEE Std 802.11-2016, 9.4.2.3
            IE_ID_SUPPORTED_RATES => {
                let rates = parse_fixed_length_ie(
                    body,
                    SUPP_RATES_IE_MIN_BODY_LEN,
                    SUPP_RATES_IE_MAX_BODY_LEN,
                )?;
                Some((InfoElement::SupportedRates(rates), remaining))
            }
            // IEEE Std 802.11-2016, 9.4.2.4
            IE_ID_DSSS_PARAM_SET => {
                let param_set = parse_fixed_length_ie(
                    body,
                    DSSS_PARAM_SET_IE_BODY_LEN,
                    DSSS_PARAM_SET_IE_BODY_LEN,
                )?;
                Some((
                    InfoElement::DsssParamSet {
                        channel: param_set[0],
                    },
                    remaining,
                ))
            }
            // IEEE Std 802.11-2016, 9.4.2.6
            IE_ID_TIM => Some((InfoElement::Tim(parse_tim(body)?), remaining)),
            // All other IEs are considered unsupported.
            id => Some((InfoElement::Unsupported { id, body }, remaining)),
        }
    }
}

fn parse_fixed_length_ie<B: ByteSlice>(body: B, min_len: usize, max_len: usize) -> Option<B> {
    if body.len() < min_len || body.len() > max_len {
        None
    } else {
        Some(body)
    }
}

fn parse_tim<B: ByteSlice>(body: B) -> Option<Tim<B>> {
    let (body, pvb) = LayoutVerified::<B, [u8; 3]>::new_unaligned_from_prefix(body)?;
    if pvb.len() < TIM_IE_MIN_PVB_LEN || pvb.len() > TIM_IE_MAX_PVB_LEN {
        None
    } else {
        Some(Tim {
            dtim_count: body[0],
            dtim_period: body[1],
            bmp_ctrl: body[2],
            partial_virtual_bmp: pvb,
        })
    }
}

// IEEE Std 802.11-2016, 9.4.2.2
fn is_wildcard_ssid<B: ByteSlice>(ssid: B) -> bool {
    ssid.len() == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_ie_chain() {
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let bytes = [
            0, 5, 1, 2, 3, 4, 5, // SSID IE
            254, 3, 1, 2, // Unsupported IE
            3, 1, 5, 5, 4, 3, 2, 1, // Supported Rates IE
            221, 222, 223, 224 // Abitrary data: this should be skipped
        ];

        // Found (SSID, Supported-Rates, Unknown)
        let mut found_ies = (false, false, false);
        let mut iter = InfoElementReader(&bytes[..]);
        for element in &mut iter {
            match element {
                InfoElement::Ssid(ssid) => {
                    assert!(!found_ies.0);
                    found_ies.0 = true;
                    assert_eq!([1, 2, 3, 4, 5], ssid);
                }
                InfoElement::SupportedRates(rates) => {
                    assert!(!found_ies.1);
                    found_ies.1 = true;
                    assert_eq!([5, 4, 3, 2, 1], rates);
                }
                InfoElement::Unsupported { id, body } => {
                    assert!(!found_ies.2);
                    found_ies.2 = true;
                    assert_eq!(254, id);
                    assert_eq!([1, 2, 3], body);
                }
                _ => panic!("unexpected IE"),
            }
        }
        assert!(found_ies.0, "SSID IE not present");
        assert!(found_ies.1, "Supported Rates IE not present");
        assert!(found_ies.2, "Unknown IE not present");

        assert_eq!([221, 222, 223, 224], iter.remaining());
    }

    #[test]
    fn parse_ssid() {
        match InfoElement::parse(&[0, 5, 1, 2, 3, 4, 5, 6, 7][..]) {
            Some((InfoElement::Ssid(ssid), remaining)) => {
                assert_eq!([1, 2, 3, 4, 5], ssid);
                assert_eq!([6, 7], remaining)
            }
            _ => panic!("error parsing SSID IE"),
        }
    }

    #[test]
    fn parse_ssid_min_max_len() {
        // min length (wildcard SSID)
        match InfoElement::parse(&[0, 0][..]) {
            Some((InfoElement::Ssid(ssid), _)) => {
                assert!(ssid.is_empty());
                assert!(is_wildcard_ssid(ssid));
            }
            _ => panic!("error parsing SSID IE"),
        }

        // max length
        let mut bytes = vec![0, 32];
        bytes.extend_from_slice(&[1; 32]);
        match InfoElement::parse(&bytes[..]) {
            Some((InfoElement::Ssid(ssid), _)) => {
                assert_eq!([1; 32], ssid);
                assert!(!is_wildcard_ssid(ssid));
            }
            _ => panic!("error parsing SSID IE"),
        }
    }

    #[test]
    fn parse_ssid_invalid() {
        // too large
        let mut bytes = vec![0, 33];
        bytes.extend_from_slice(&[1; 33]);
        assert!(InfoElement::parse(&bytes[..]).is_none());

        // corrupted
        assert!(InfoElement::parse(&[0, 5, 1, 2][..]).is_none());
    }

    #[test]
    fn parse_supported_rates() {
        match InfoElement::parse(&[1, 5, 1, 2, 3, 4, 5, 6, 7][..]) {
            Some((InfoElement::SupportedRates(rates), remaining)) => {
                assert_eq!([1, 2, 3, 4, 5], rates);
                assert_eq!([6, 7], remaining)
            }
            _ => panic!("error parsing supported rates IE"),
        }
    }

    #[test]
    fn parse_supported_rates_min_max_le() {
        // min length
        match InfoElement::parse(&[1, 1, 1][..]) {
            Some((InfoElement::SupportedRates(rates), _)) => assert_eq!([1], rates),
            _ => panic!("error parsing supported rates IE"),
        }

        // max length
        match InfoElement::parse(&[1, 8, 1, 2, 3, 4, 5, 6, 7, 8][..]) {
            Some((InfoElement::SupportedRates(rates), _)) => {
                assert_eq!([1, 2, 3, 4, 5, 6, 7, 8], rates)
            }
            _ => panic!("error parsing supported rates IE"),
        }
    }

    #[test]
    fn parse_supported_rates_invalid() {
        // too short
        assert!(InfoElement::parse(&[1, 0][..]).is_none());

        // too large
        let mut bytes = vec![1, 9];
        bytes.extend_from_slice(&[1; 9]);
        assert!(InfoElement::parse(&bytes[..]).is_none());

        // corrupted
        assert!(InfoElement::parse(&[1, 5, 1, 2][..]).is_none());
    }

    #[test]
    fn parse_dsss_param_set() {
        match InfoElement::parse(&[3, 1, 11, 6, 7][..]) {
            Some((InfoElement::DsssParamSet { channel }, remaining)) => {
                assert_eq!(11, channel);
                assert_eq!([6, 7], remaining)
            }
            _ => panic!("error parsing DSSS param set IE"),
        }
    }

    #[test]
    fn parse_dsss_param_set_invalid() {
        // too long
        assert!(InfoElement::parse(&[3, 2, 1, 2][..]).is_none());

        // too short
        assert!(InfoElement::parse(&[3, 0][..]).is_none());

        // corrupted
        assert!(InfoElement::parse(&[3, 1][..]).is_none());
    }

    #[test]
    fn parse_tim() {
        match InfoElement::parse(&[5, 6, 1, 2, 3, 4, 5, 6, 7, 8][..]) {
            Some((InfoElement::Tim(tim), remaining)) => {
                assert_eq!(1, tim.dtim_count);
                assert_eq!(2, tim.dtim_period);
                assert_eq!(3, tim.bmp_ctrl);
                assert_eq!([4, 5, 6], tim.partial_virtual_bmp);

                assert_eq!([7, 8], remaining)
            }
            _ => panic!("error parsing TIM IE"),
        }
    }

    #[test]
    fn parse_tim_min_max_len() {
        // min length
        match InfoElement::parse(&[5, 4, 1, 2, 3, 4][..]) {
            Some((InfoElement::Tim(tim), _)) => {
                assert_eq!(1, tim.dtim_count);
                assert_eq!(2, tim.dtim_period);
                assert_eq!(3, tim.bmp_ctrl);
                assert_eq!([4], tim.partial_virtual_bmp);
            }
            _ => panic!("error parsing TIM IE"),
        }

        // max length
        let mut bytes = vec![5, 254];
        bytes.extend_from_slice(&[1; 254]);
        match InfoElement::parse(&bytes[..]) {
            Some((InfoElement::Tim(tim), _)) => {
                assert_eq!(1, tim.dtim_count);
                assert_eq!(1, tim.dtim_period);
                assert_eq!(1, tim.bmp_ctrl);
                assert_eq!(&[1; 251][..], tim.partial_virtual_bmp);
            }
            _ => panic!("error parsing TIM IE"),
        }
    }

    #[test]
    fn parse_tim_invalid() {
        // too short
        assert!(InfoElement::parse(&[5, 3, 1, 2, 3][..]).is_none());

        // too long
        let mut bytes = vec![5, 255];
        bytes.extend_from_slice(&[1; 255]);
        assert!(InfoElement::parse(&bytes[..]).is_none());

        // corrupted
        assert!(InfoElement::parse(&[5, 3, 1, 2][..]).is_none());
    }

    #[test]
    fn parse_unsupported_ie() {
        match InfoElement::parse(&[254, 3, 1, 2, 3, 6, 7][..]) {
            Some((InfoElement::Unsupported { id, body }, remaining)) => {
                assert_eq!(254, id);
                assert_eq!([1, 2, 3], body);
                assert_eq!([6, 7], remaining);
            }
            _ => panic!("error parsing unknown IE"),
        }
    }
}
