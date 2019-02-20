// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer_writer::BufferWriter,
    bitfield::bitfield,
    failure::{bail, ensure, Error},
    zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned},
};

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
const IE_ID_SSID: u8 = 0;
const IE_ID_SUPPORTED_RATES: u8 = 1;
const IE_ID_DSSS_PARAM_SET: u8 = 3;
const IE_ID_TIM: u8 = 5;
const IE_ID_EXT_SUPPORTED_RATES: u8 = 50;

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

// IEEE Std 802.11-2016, 9.4.2.13
const EXT_SUPP_RATES_IE_MIN_BODY_LEN: usize = 1;
const EXT_SUPP_RATES_IE_MAX_BODY_LEN: usize = 255;

// IEEE Std 802.11-2016, 9.4.2.1
const IE_HDR_LEN: usize = 2;
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
const TIM_FIXED_FIELD_BYTES: usize = 3;
pub struct Tim<B: ByteSlice> {
    pub dtim_count: u8,
    pub dtim_period: u8,
    pub bmp_ctrl: u8,
    pub partial_virtual_bmp: B,
}

impl<B: ByteSlice> Tim<B> {
    pub fn len(&self) -> usize {
        TIM_FIXED_FIELD_BYTES + &self.partial_virtual_bmp[..].len()
    }

    pub fn is_traffic_buffered(&self, aid: usize) -> bool {
        let n1 = BitmapControl(self.bmp_ctrl).offset() as usize * 2;
        let octet = aid / 8;

        let pvb = &self.partial_virtual_bmp[..];

        let carries_aid = n1 <= octet && octet < pvb.len() + n1;
        carries_aid && pvb[octet - n1] & (1 << (aid % 8)) != 0
    }
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

pub struct InfoElementWriter<B: ByteSliceMut> {
    w: BufferWriter<B>,
}

impl<B: ByteSliceMut> InfoElementWriter<B> {
    pub fn new(w: BufferWriter<B>) -> InfoElementWriter<B> {
        InfoElementWriter { w }
    }

    pub fn write_ssid(mut self, ssid: &[u8]) -> Result<Self, Error> {
        ensure!(ssid.len() <= SSID_IE_MAX_BODY_LEN, "SSID '{:x?}' > 32 bytes", ssid);
        ensure!(
            self.w.remaining_bytes() >= IE_HDR_LEN + ssid.len(),
            "buffer too short to write SSID IE"
        );
        let w = self.w.write_bytes(&[IE_ID_SSID, ssid.len() as u8])?.write_bytes(ssid)?;
        Ok(InfoElementWriter { w })
    }

    pub fn write_supported_rates(mut self, rates: &[u8]) -> Result<Self, Error> {
        ensure!(rates.len() >= SUPP_RATES_IE_MIN_BODY_LEN, "supported rates is empty",);
        ensure!(
            rates.len() <= SUPP_RATES_IE_MAX_BODY_LEN,
            "too many supported rates; max: {}, got: {}",
            SUPP_RATES_IE_MAX_BODY_LEN,
            rates.len()
        );
        ensure!(
            self.w.remaining_bytes() >= IE_HDR_LEN + rates.len(),
            "buffer too short to write supported rates IE"
        );

        let w =
            self.w.write_bytes(&[IE_ID_SUPPORTED_RATES, rates.len() as u8])?.write_bytes(rates)?;
        Ok(InfoElementWriter { w })
    }

    pub fn write_dsss_param_set(mut self, chan: u8) -> Result<Self, Error> {
        ensure!(
            self.w.remaining_bytes() >= IE_HDR_LEN + DSSS_PARAM_SET_IE_BODY_LEN,
            "buffer too short to write DSSS param set IE"
        );
        let w =
            self.w.write_bytes(&[IE_ID_DSSS_PARAM_SET, DSSS_PARAM_SET_IE_BODY_LEN as u8, chan])?;
        Ok(InfoElementWriter { w })
    }

    pub fn write_tim<C: ByteSliceMut>(mut self, tim: &Tim<C>) -> Result<Self, Error> {
        ensure!(
            tim.partial_virtual_bmp.len() >= TIM_IE_MIN_PVB_LEN,
            "partial virtual bitmap is empty",
        );
        ensure!(
            tim.partial_virtual_bmp.len() <= TIM_IE_MAX_PVB_LEN,
            "partial virtual bitmap too large; max: {}, got {}",
            TIM_IE_MAX_PVB_LEN,
            tim.partial_virtual_bmp.len()
        );
        ensure!(
            self.w.remaining_bytes() >= IE_HDR_LEN + tim.len(),
            "buffer too short to write TIM IE"
        );

        let w = self
            .w
            .write_bytes(&[IE_ID_TIM, tim.len() as u8])?
            .write_bytes(&[tim.dtim_count, tim.dtim_period, tim.bmp_ctrl])?
            .write_bytes(&tim.partial_virtual_bmp[..])?;
        Ok(InfoElementWriter { w })
    }

    pub fn close(mut self) -> BufferWriter<B> {
        self.w
    }
}

pub enum InfoElement<B: ByteSlice> {
    // IEEE Std 802.11-2016, 9.4.2.2
    Ssid(B),
    // IEEE Std 802.11-2016, 9.4.2.3
    SupportedRates(B),
    // IEEE Std 802.11-2016, 9.4.2.4
    DsssParamSet { channel: u8 },
    // IEEE Std 802.11-2016, 9.4.2.6
    Tim(Tim<B>),
    // IEEE Std 802.11-2016, 9.4.2.13
    ExtSupportedRates(B),
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
                let ssid = parse_ie_with_bounds(body, SSID_IE_MIN_BODY_LEN, SSID_IE_MAX_BODY_LEN)?;
                Some((InfoElement::Ssid(ssid), remaining))
            }
            // IEEE Std 802.11-2016, 9.4.2.3
            IE_ID_SUPPORTED_RATES => {
                let rates = parse_ie_with_bounds(
                    body,
                    SUPP_RATES_IE_MIN_BODY_LEN,
                    SUPP_RATES_IE_MAX_BODY_LEN,
                )?;
                Some((InfoElement::SupportedRates(rates), remaining))
            }
            // IEEE Std 802.11-2016, 9.4.2.4
            IE_ID_DSSS_PARAM_SET => {
                let param_set = parse_ie_with_bounds(
                    body,
                    DSSS_PARAM_SET_IE_BODY_LEN,
                    DSSS_PARAM_SET_IE_BODY_LEN,
                )?;
                Some((InfoElement::DsssParamSet { channel: param_set[0] }, remaining))
            }
            // IEEE Std 802.11-2016, 9.4.2.6
            IE_ID_TIM => Some((InfoElement::Tim(parse_tim(body)?), remaining)),
            // IEEE Std 802.11-2016, 9.4.2.13
            IE_ID_EXT_SUPPORTED_RATES => {
                let rates = parse_ie_with_bounds(
                    body,
                    EXT_SUPP_RATES_IE_MIN_BODY_LEN,
                    EXT_SUPP_RATES_IE_MAX_BODY_LEN,
                )?;
                Some((InfoElement::ExtSupportedRates(rates), remaining))
            }
            // All other IEs are considered unsupported.
            id => Some((InfoElement::Unsupported { id, body }, remaining)),
        }
    }
}

fn parse_ie_with_bounds<B: ByteSlice>(body: B, min_len: usize, max_len: usize) -> Option<B> {
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
        #[rustfmt::skip]
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

    #[test]
    fn write_parse_many_ies() {
        let mut buf = vec![222u8; 510];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_ssid("fuchsia".as_bytes())
            .expect("error writing SSID IE")
            .write_supported_rates(&[1u8, 2, 3, 4])
            .expect("error writing supported rates IE")
            .write_dsss_param_set(7)
            .expect("error writing DSSS param set IE")
            .write_tim(&Tim {
                dtim_count: 1,
                dtim_period: 2,
                bmp_ctrl: 3,
                partial_virtual_bmp: &mut [5u8; 10][..],
            })
            .expect("error writing TIM IE");

        // Found (SSID, Supported-Rates, DSSS, Tim)
        let mut found_ies = (false, false, false, false);
        let mut iter = InfoElementReader(&buf[..]);
        for element in &mut iter {
            match element {
                InfoElement::Ssid(ssid) => {
                    assert!(!found_ies.0);
                    found_ies.0 = true;
                    assert_eq!("fuchsia".as_bytes(), ssid);
                }
                InfoElement::SupportedRates(rates) => {
                    assert!(!found_ies.1);
                    found_ies.1 = true;
                    assert_eq!([1, 2, 3, 4], rates);
                }
                InfoElement::DsssParamSet { channel } => {
                    assert!(!found_ies.2);
                    found_ies.2 = true;
                    assert_eq!(7, channel);
                }
                InfoElement::Tim(tim) => {
                    assert!(!found_ies.3);
                    found_ies.3 = true;
                    assert_eq!(1, tim.dtim_count);
                    assert_eq!(2, tim.dtim_period);
                    assert_eq!(3, tim.bmp_ctrl);
                    assert_eq!(&[5u8; 10], tim.partial_virtual_bmp);
                }
                _ => (),
            }
        }
        assert!(found_ies.0, "SSID IE not present");
        assert!(found_ies.1, "Supported Rates IE not present");
        assert!(found_ies.2, "DSSS IE not present");
        assert!(found_ies.3, "TIM IE not present");
    }

    #[test]
    fn write_ssid() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_ssid("fuchsia".as_bytes())
            .expect("error writing SSID IE");
        assert_eq!(&buf[..2][..], &[0u8, 7][..]);
        assert_eq!(&buf[2..9][..], "fuchsia".as_bytes());
        assert!(is_zero(&buf[9..]));
    }

    #[test]
    fn write_ssid_wildcard() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_ssid(&[])
            .expect("error writing SSID IE");
        assert!(is_zero(&buf[2..]));
    }

    #[test]
    fn write_ssid_buffer_too_short() {
        let mut buf = vec![0u8; 4];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_ssid("fuchsia".as_bytes());
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_ssid_too_large() {
        let mut buf = vec![0u8; 50];
        let result =
            InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_ssid(&[4u8; 33][..]);
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_supported_rates() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_supported_rates(&[1u8, 2, 3, 4])
            .expect("error writing supported rates IE");
        assert_eq!(&buf[..2][..], &[1u8, 4][..]);
        assert_eq!(&buf[2..6], &[1u8, 2, 3, 4][..]);
        assert!(is_zero(&buf[7..]));
    }

    #[test]
    fn write_supported_rates_buffer_too_short() {
        let mut buf = vec![0u8; 4];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_supported_rates(&[1u8, 2, 3, 4]);
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_supported_rates_max_rates() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_supported_rates(&[1u8, 2, 3, 4, 5, 6, 7, 8])
            .expect("error writing supported rates IE");
        assert_eq!(&buf[..2][..], &[1u8, 8][..]);
        assert_eq!(&buf[2..10], &[1u8, 2, 3, 4, 5, 6, 7, 8][..]);
        assert!(is_zero(&buf[10..]));
    }

    #[test]
    fn write_supported_rates_min_rates() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_supported_rates(&[1u8])
            .expect("error writing supported rates IE");
        assert_eq!(&buf[..2][..], &[1u8, 1][..]);
        assert_eq!(buf[2], 1u8);
        assert!(is_zero(&buf[3..]));
    }

    #[test]
    fn write_supported_rates_no_rates() {
        let mut buf = vec![0u8; 50];
        let result =
            InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_supported_rates(&[]);
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_supported_rates_too_many_rates() {
        let mut buf = vec![0u8; 50];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_supported_rates(&[1u8, 2, 3, 4, 5, 6, 7, 8, 9]);
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_dsss_param_set() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_dsss_param_set(77)
            .expect("error writing DSSS param set IE");
        assert_eq!(&buf[..2][..], &[3u8, 1][..]);
        assert_eq!(buf[2], 77);
        assert!(is_zero(&buf[3..]));
    }

    #[test]
    fn write_dsss_param_set_buffer_too_short() {
        let mut buf = vec![0u8; 2];
        let result =
            InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_dsss_param_set(77);
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_tim() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_tim(&Tim {
                dtim_count: 1,
                dtim_period: 2,
                bmp_ctrl: 3,
                partial_virtual_bmp: &mut [1u8, 2, 3, 4, 5, 6][..],
            })
            .expect("error writing TIM IE");
        assert_eq!(&buf[..2][..], &[5u8, 9][..]);
        assert_eq!(buf[2], 1);
        assert_eq!(buf[3], 2);
        assert_eq!(buf[4], 3);
        assert_eq!(&buf[5..11][..], &[1u8, 2, 3, 4, 5, 6][..]);
        assert!(is_zero(&buf[11..]));
    }

    #[test]
    fn write_tim_too_short_buffer() {
        let mut buf = vec![0u8; 5];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_tim(&Tim {
            dtim_count: 1,
            dtim_period: 2,
            bmp_ctrl: 3,
            partial_virtual_bmp: &mut [1u8, 2, 3, 4, 5, 6][..],
        });
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_tim_too_short_pvb() {
        let mut buf = vec![0u8; 50];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_tim(&Tim {
            dtim_count: 1,
            dtim_period: 2,
            bmp_ctrl: 3,
            partial_virtual_bmp: &mut [][..],
        });
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_tim_too_large_pvb() {
        let mut buf = vec![0u8; 50];
        let result = InfoElementWriter::new(BufferWriter::new(&mut buf[..])).write_tim(&Tim {
            dtim_count: 1,
            dtim_period: 2,
            bmp_ctrl: 3,
            partial_virtual_bmp: &mut [3u8; 252][..],
        });
        assert!(result.is_err());
        assert!(is_zero(&buf[..]));
    }

    #[test]
    fn write_tim_min_pvb() {
        let mut buf = vec![0u8; 50];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_tim(&Tim {
                dtim_count: 1,
                dtim_period: 2,
                bmp_ctrl: 3,
                partial_virtual_bmp: &mut [5u8][..],
            })
            .expect("error writing TIM IE");
        assert_eq!(&buf[..2][..], &[5u8, 4][..]);
        assert_eq!(buf[2], 1);
        assert_eq!(buf[3], 2);
        assert_eq!(buf[4], 3);
        assert_eq!(buf[5], 5);
        assert!(is_zero(&buf[6..]));
    }

    #[test]
    fn write_tim_max_pvb() {
        let mut buf = vec![0u8; 300];
        InfoElementWriter::new(BufferWriter::new(&mut buf[..]))
            .write_tim(&Tim {
                dtim_count: 1,
                dtim_period: 2,
                bmp_ctrl: 3,
                partial_virtual_bmp: &mut [5u8; 251][..],
            })
            .expect("error writing TIM IE");
        assert_eq!(&buf[..2][..], &[5u8, 254][..]);
        assert_eq!(buf[2], 1);
        assert_eq!(buf[3], 2);
        assert_eq!(buf[4], 3);
        assert_eq!(&buf[5..256], &[5u8; 251][..]);
        assert!(is_zero(&buf[257..]));
    }

    #[test]
    fn parse_ext_supported_rates() {
        match InfoElement::parse(&[50, 5, 1, 2, 3, 4, 5, 6, 7][..]) {
            Some((InfoElement::ExtSupportedRates(rates), remaining)) => {
                assert_eq!([1, 2, 3, 4, 5], rates);
                assert_eq!([6, 7], remaining)
            }
            _ => panic!("error parsing extended supported rates IE"),
        }
    }

    #[test]
    fn parse_ext_supported_rates_min_max_len() {
        // min length
        match InfoElement::parse(&[50, 1, 8][..]) {
            Some((InfoElement::ExtSupportedRates(rates), _)) => assert_eq!([8], rates),
            _ => panic!("error parsing extended supported rates IE"),
        }

        // max length
        let mut bytes = vec![50, 255];
        bytes.extend_from_slice(&[9; 255]);
        match InfoElement::parse(&bytes[..]) {
            Some((InfoElement::ExtSupportedRates(rates), _)) => {
                assert_eq!(&[9; 255][..], &rates[..])
            }
            _ => panic!("error parsing extended supported rates IE"),
        }
    }

    #[test]
    fn parse_ext_supported_rates_invalid() {
        // too short
        assert!(InfoElement::parse(&[50, 0][..]).is_none());

        // corrupted
        assert!(InfoElement::parse(&[50, 5, 1, 2][..]).is_none());
    }

    #[test]
    fn is_traffic_buffered() {
        let tim = Tim {
            dtim_period: 0,
            dtim_count: 0,
            bmp_ctrl: 0,
            partial_virtual_bmp: &[0b0010010][..],
        };
        assert!(!tim.is_traffic_buffered(0));
        assert!(tim.is_traffic_buffered(1));
        assert!(!tim.is_traffic_buffered(2));
        assert!(!tim.is_traffic_buffered(3));
        assert!(tim.is_traffic_buffered(4));
        assert!(!tim.is_traffic_buffered(5));
        assert!(!tim.is_traffic_buffered(6));
        assert!(!tim.is_traffic_buffered(7));
        assert!(!tim.is_traffic_buffered(100));

        let mut bmp_ctrl = BitmapControl(0);
        bmp_ctrl.set_offset(1);
        let tim = Tim { bmp_ctrl: bmp_ctrl.value(), ..tim };
        // Offset of 1 means "skip 16 bits"
        assert!(!tim.is_traffic_buffered(15));
        assert!(!tim.is_traffic_buffered(16));
        assert!(tim.is_traffic_buffered(17));
        assert!(!tim.is_traffic_buffered(18));
        assert!(!tim.is_traffic_buffered(19));
        assert!(tim.is_traffic_buffered(20));
        assert!(!tim.is_traffic_buffered(21));
        assert!(!tim.is_traffic_buffered(22));
        assert!(!tim.is_traffic_buffered(100));
    }

    pub fn is_zero(slice: &[u8]) -> bool {
        slice.iter().all(|&x| x == 0)
    }
}
