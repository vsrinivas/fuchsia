// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{akm, cipher, pmkid, suite_selector};

use crate::appendable::{Appendable, BufferTooSmall};
use crate::organization::Oui;
use bytes::Bytes;
use nom::combinator::{map, map_res};
use nom::number::streaming::{le_u16, le_u8};
use nom::{call, cond, count, do_parse, eof, named, named_attr, take, try_parse, IResult};
use wlan_bitfield::bitfield;

macro_rules! if_remaining (
  ($i:expr, $f:expr) => ( cond!($i, $i.len() !=0, call!($f)); );
);

// IEEE 802.11-2016, 9.4.2.25.1
pub const ID: u8 = 48;
pub const VERSION: u16 = 1;

// IEEE 802.11-2016, 9.4.2.25.1
#[derive(Default, Debug, PartialOrd, PartialEq, Clone)]
pub struct Rsne {
    pub version: u16,
    pub group_data_cipher_suite: Option<cipher::Cipher>,
    pub pairwise_cipher_suites: Vec<cipher::Cipher>,
    pub akm_suites: Vec<akm::Akm>,
    pub rsn_capabilities: Option<RsnCapabilities>,
    pub pmkids: Vec<pmkid::Pmkid>,
    pub group_mgmt_cipher_suite: Option<cipher::Cipher>,
}

#[bitfield(
    0         preauth,
    1         no_pairwise,
    2..=3     ptksa_replay_counter,
    4..=5     gtksa_replay_counter,
    6         mgmt_frame_protection_req,
    7         mgmt_frame_protection_cap,
    8         joint_multiband,
    9         peerkey_enabled,
    10        ssp_amsdu_cap,
    11        ssp_amsdu_req,
    12        pbac,
    13        extended_key_id,
    14..=15   _, // reserved
)]
#[derive(PartialOrd, PartialEq, Clone)]
pub struct RsnCapabilities(pub u16);

impl Rsne {
    pub fn new() -> Self {
        let mut rsne = Rsne::default();
        rsne.version = VERSION;
        rsne
    }

    pub fn len(&self) -> usize {
        let mut length: usize = 4;
        match self.group_data_cipher_suite.as_ref() {
            None => return length,
            Some(_) => length += 4,
        };

        if self.pairwise_cipher_suites.is_empty() {
            return length;
        }
        length += 2 + 4 * self.pairwise_cipher_suites.len();

        if self.akm_suites.is_empty() {
            return length;
        }
        length += 2 + 4 * self.akm_suites.len();

        match self.rsn_capabilities.as_ref() {
            None => return length,
            Some(_) => length += 2,
        };

        if self.pmkids.is_empty() && self.group_mgmt_cipher_suite.is_none() {
            return length;
        }
        length += 2 + 16 * self.pmkids.len();

        length += match self.group_mgmt_cipher_suite.as_ref() {
            None => 0,
            Some(_) => 4,
        };
        length
    }

    pub fn write_into<A: Appendable>(&self, buf: &mut A) -> Result<(), BufferTooSmall> {
        if !buf.can_append(self.len()) {
            return Err(BufferTooSmall);
        }

        buf.append_byte(ID)?;
        buf.append_byte((self.len() - 2) as u8)?;
        buf.append_value(&self.version)?;

        match self.group_data_cipher_suite.as_ref() {
            None => return Ok(()),
            Some(cipher) => {
                buf.append_bytes(&cipher.oui[..])?;
                buf.append_byte(cipher.suite_type)?;
            }
        };

        if self.pairwise_cipher_suites.is_empty() {
            return Ok(());
        }
        buf.append_value(&(self.pairwise_cipher_suites.len() as u16))?;
        for cipher in &self.pairwise_cipher_suites {
            buf.append_bytes(&cipher.oui[..])?;
            buf.append_byte(cipher.suite_type)?;
        }

        if self.akm_suites.is_empty() {
            return Ok(());
        }
        buf.append_value(&(self.akm_suites.len() as u16))?;
        for akm in &self.akm_suites {
            buf.append_bytes(&akm.oui[..])?;
            buf.append_byte(akm.suite_type)?;
        }

        match self.rsn_capabilities.as_ref() {
            None => return Ok(()),
            Some(caps) => buf.append_value(&caps.0)?,
        };

        if self.pmkids.is_empty() && self.group_mgmt_cipher_suite.is_none() {
            return Ok(());
        }
        buf.append_value(&(self.pmkids.len() as u16))?;
        for pmkid in &self.pmkids {
            buf.append_bytes(&pmkid[..])?;
        }

        if let Some(cipher) = self.group_mgmt_cipher_suite.as_ref() {
            buf.append_bytes(&cipher.oui[..])?;
            buf.append_byte(cipher.suite_type)?;
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

fn read_pmkid(input: &[u8]) -> IResult<&[u8], pmkid::Pmkid> {
    let f = |bytes| {
        let pmkid_data = Bytes::copy_from_slice(bytes);
        return pmkid::new(pmkid_data);
    };

    map_res(nom::bytes::streaming::take(16usize), f)(input)
}

named!(akm<&[u8], akm::Akm>, call!(read_suite_selector::<akm::Akm>));
named!(cipher<&[u8], cipher::Cipher>, call!(read_suite_selector::<cipher::Cipher>));

named_attr!(
    /// convert bytes of an RSNE information element into an RSNE representation. This method
    /// does not depend on the information element length field (second byte) and thus does not
    /// validate that it's correct
    , // comma ends the attribute list to named_attr
    pub from_bytes<&[u8], Rsne>,
       do_parse!(
           _element_id: le_u8 >>
           _length: le_u8 >>
           version: le_u16 >>
           group_cipher: if_remaining!(cipher) >>
           pairwise_count: if_remaining!(le_u16) >>
           pairwise_list: count!(cipher, pairwise_count.unwrap_or(0) as usize)  >>
           akm_count: if_remaining!(le_u16) >>
           akm_list: count!(akm, akm_count.unwrap_or(0) as usize)  >>
           rsn_capabilities: if_remaining!(map(le_u16, RsnCapabilities)) >>
           pmkid_count: if_remaining!(le_u16) >>
           pmkid_list: count!(read_pmkid, pmkid_count.unwrap_or(0) as usize)  >>
           group_mgmt_cipher_suite: if_remaining!(cipher) >>
           eof!() >>
           (Rsne{
                version: version,
                group_data_cipher_suite: group_cipher,
                pairwise_cipher_suites: pairwise_list,
                akm_suites: akm_list,
                rsn_capabilities: rsn_capabilities,
                pmkids: pmkid_list,
                group_mgmt_cipher_suite: group_mgmt_cipher_suite
           })
    )
);

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::FixedSizedTestBuffer;

    #[cfg(feature = "benchmark")]
    mod bench {
        use self::test::Bencher;
        use super::*;
        #[cfg()]
        #[bench]
        fn bench_parse_with_nom(b: &mut Bencher) {
            let frame: Vec<u8> = vec![
                0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0xa8, 0x04, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
                0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
                0xac, 0x04,
            ];
            b.iter(|| from_bytes(&frame));
        }
    }

    #[test]
    fn test_write_into() {
        let frame: Vec<u8> = vec![
            0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0xa8, 0x04, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
            0xac, 0x04,
        ];
        let mut buf = Vec::with_capacity(128);
        let result = from_bytes(&frame);
        assert!(result.is_ok());
        let rsne = result.unwrap().1;
        rsne.write_into(&mut buf).expect("failed writing RSNE");
        let rsne_len = buf.len();
        let left_over = buf.split_off(rsne_len);
        assert_eq!(&buf[..], &frame[..]);
        assert!(left_over.iter().all(|b| *b == 0));
    }

    #[test]
    fn test_short_buffer() {
        let frame: Vec<u8> = vec![
            0x30, 0x2A, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0xa8, 0x04, 0x01, 0x00, 0x01, 0x02, 0x03, 0x04,
            0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x00, 0x0f,
            0xac, 0x04,
        ];
        let mut buf = FixedSizedTestBuffer::new(32);
        let result = from_bytes(&frame);
        assert!(result.is_ok());
        let rsne = result.unwrap().1;
        rsne.write_into(&mut buf).expect_err("expected writing RSNE to fail");
        assert_eq!(buf.bytes_written(), 0);
    }

    #[test]
    fn test_rsn_fields_representation() {
        let frame: Vec<u8> = vec![
            0x30, // element id
            0x2A, // length
            0x01, 0x00, // version
            0x00, 0x0f, 0xac, 0x04, // group data cipher suite
            0x01, 0x00, // pairwise cipher suite count
            0x00, 0x0f, 0xac, 0x04, // pairwise cipher suite list
            0x01, 0x00, // akm suite count
            0x00, 0x0f, 0xac, 0x02, // akm suite list
            0xa8, 0x04, // rsn capabilities
            0x01, 0x00, // pmk id count
            // pmk id list
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11, 0x00, 0x0f, 0xac, 0x04, // group management cipher suite
        ];
        let result = from_bytes(&frame);
        assert!(result.is_ok());
        let rsne = result.unwrap().1;

        assert_eq!(rsne.version, 1);
        assert_eq!(rsne.len(), 0x2a + 2);

        assert!(rsne.group_data_cipher_suite.is_some());
        assert_eq!(
            rsne.group_data_cipher_suite,
            Some(cipher::Cipher { oui: Oui::DOT11, suite_type: cipher::CCMP_128 })
        );
        assert_eq!(rsne.pairwise_cipher_suites.len(), 1);
        assert_eq!(rsne.pairwise_cipher_suites[0].oui, Oui::DOT11);
        assert_eq!(rsne.pairwise_cipher_suites[0].suite_type, cipher::CCMP_128);
        assert_eq!(rsne.akm_suites.len(), 1);
        assert_eq!(rsne.akm_suites[0].suite_type, akm::PSK);

        let rsn_capabilities = rsne.rsn_capabilities.expect("should have RSN capabilities");
        assert_eq!(rsn_capabilities.preauth(), false);
        assert_eq!(rsn_capabilities.no_pairwise(), false);
        assert_eq!(rsn_capabilities.ptksa_replay_counter(), 2);
        assert_eq!(rsn_capabilities.gtksa_replay_counter(), 2);
        assert!(!rsn_capabilities.mgmt_frame_protection_req());
        assert!(rsn_capabilities.mgmt_frame_protection_cap());
        assert!(!rsn_capabilities.joint_multiband());
        assert!(!rsn_capabilities.peerkey_enabled());
        assert!(rsn_capabilities.ssp_amsdu_cap());
        assert!(!rsn_capabilities.ssp_amsdu_req());
        assert!(!rsn_capabilities.pbac());
        assert!(!rsn_capabilities.extended_key_id());

        assert_eq!(rsn_capabilities.0, 0xa8 + (0x04 << 8));

        let pmkids: &[u8] = &[
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11,
        ];
        assert_eq!(rsne.pmkids.len(), 1);
        assert_eq!(rsne.pmkids[0], Bytes::from(pmkids));

        assert_eq!(
            rsne.group_mgmt_cipher_suite,
            Some(cipher::Cipher { oui: Oui::DOT11, suite_type: cipher::CCMP_128 })
        );
    }

    #[test]
    fn test_rsn_capabilities_setters() {
        let mut rsn_caps = RsnCapabilities(0u16);
        rsn_caps.set_ptksa_replay_counter(2);
        rsn_caps.set_gtksa_replay_counter(2);
        rsn_caps.set_mgmt_frame_protection_cap(true);
        rsn_caps.set_ssp_amsdu_cap(true);

        assert_eq!(rsn_caps.0, 0xa8 + (0x04 << 8));
    }
}
