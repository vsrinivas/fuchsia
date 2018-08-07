// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use akm::Akm;
use bytes::Bytes;
use cipher::Cipher;
use crypto_utils::prf;
use failure;
use std::cmp::{max, min};

/// A PTK is derived from a PMK and provides access to the PTK's key-hierarchy which yields a KEK,
/// KCK, and TK, used for EAPOL frame protection, integrity check and unicast frame protection
/// respectively.
#[derive(Debug, Clone, PartialEq)]
pub struct Ptk {
    ptk: Vec<u8>,
    kck_len: usize,
    kek_len: usize,
    tk_len: usize,
    // TODO(hahnr): Add TKIP Tx/Rx MIC support (IEEE 802.11-2016, 12.8.1).
}

impl Ptk {
    // IEEE 802.11-2016, 12.7.1.3
    pub fn new(
        pmk: &[u8],
        aa: &[u8; 6],
        spa: &[u8; 6],
        anonce: &[u8],
        snonce: &[u8],
        akm: &Akm,
        cipher: &Cipher,
    ) -> Result<Ptk, failure::Error> {
        if anonce.len() != 32 {
            return Err(Error::InvalidNonceSize(anonce.len()).into());
        }
        if snonce.len() != 32 {
            return Err(Error::InvalidNonceSize(snonce.len()).into());
        }

        let pmk_bits = akm.pmk_bits()
            .ok_or_else(|| failure::Error::from(Error::PtkHierarchyUnsupportedAkmError))?;
        if pmk.len() != (pmk_bits / 8) as usize {
            return Err(Error::PtkHierarchyInvalidPmkError.into());
        }

        let kck_bits = akm.kck_bits()
            .ok_or_else(|| failure::Error::from(Error::PtkHierarchyUnsupportedAkmError))?;
        let kek_bits = akm.kek_bits()
            .ok_or_else(|| failure::Error::from(Error::PtkHierarchyUnsupportedAkmError))?;
        let tk_bits = cipher
            .tk_bits()
            .ok_or_else(|| failure::Error::from(Error::PtkHierarchyUnsupportedCipherError))?;
        let prf_bits = kck_bits + kek_bits + tk_bits;

        // data length = 6 (aa) + 6 (spa) + 32 (anonce) + 32 (snonce)
        let mut data: [u8; 76] = [0; 76];
        data[0..6].copy_from_slice(&min(aa, spa)[..]);
        data[6..12].copy_from_slice(&max(aa, spa)[..]);
        data[12..44].copy_from_slice(&min(anonce, snonce)[..]);
        data[44..].copy_from_slice(&max(anonce, snonce)[..]);

        // Use PRF to derive the PTK from the PMK while grants access to the KEK, KCK and TK.
        let ptk_bytes = prf(pmk, "Pairwise key expansion", &data, prf_bits as usize)?;
        let ptk = Ptk {
            ptk: ptk_bytes,
            kck_len: (kck_bits / 8) as usize,
            kek_len: (kek_bits / 8) as usize,
            tk_len: (tk_bits / 8) as usize,
        };
        Ok(ptk)
    }

    pub fn kck(&self) -> &[u8] {
        &self.ptk[0..self.kck_len]
    }

    pub fn kek(&self) -> &[u8] {
        let start = self.kck_len;
        &self.ptk[start..start + self.kek_len]
    }

    pub fn tk(&self) -> &[u8] {
        let start = self.kck_len + self.kek_len;
        &self.ptk[start..start + self.tk_len]
    }

    pub fn ptk(&self) -> &[u8] {
        &self.ptk[..]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use akm::{Akm, PSK};
    use cipher::{CCMP_128, Cipher, TKIP};
    use hex::FromHex;
    use suite_selector::{Factory, OUI};

    struct TestData {
        pmk: Vec<u8>,
        aa: [u8; 6],
        spa: [u8; 6],
        anonce: [u8; 32],
        snonce: [u8; 32],
    }

    // IEEE Std 802.11-2016, J.7.1, Table J-13
    fn ieee_test_data() -> TestData {
        let pmk = Vec::from_hex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af")
            .unwrap();
        let aa = <[u8; 6]>::from_hex("a0a1a1a3a4a5").unwrap();
        let spa = <[u8; 6]>::from_hex("b0b1b2b3b4b5").unwrap();
        let anonce = <[u8; 32]>::from_hex(
            "e0e1e2e3e4e5e6e7e8e9f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405",
        ).unwrap();
        let snonce = <[u8; 32]>::from_hex(
            "c0c1c2c3c4c5c6c7c8c9d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5",
        ).unwrap();
        TestData {
            pmk,
            aa,
            spa,
            anonce,
            snonce,
        }
    }

    fn new_ptk(data: &TestData, akm_suite: u8, cipher_suite: u8) -> Result<Ptk, failure::Error> {
        let akm = Akm::new(Bytes::from(&OUI[..]), akm_suite).unwrap();
        let cipher = Cipher::new(Bytes::from(&OUI[..]), cipher_suite).unwrap();
        Ptk::new(
            &data.pmk[..],
            &data.aa,
            &data.spa,
            &data.anonce,
            &data.snonce,
            &akm,
            &cipher,
        )
    }

    // IEEE Std 802.11-2016, J.7.1 & J.7.2
    #[test]
    fn test_pairwise_key_hierarchy_ccmp() {
        let data = ieee_test_data();
        let ptk_result = new_ptk(&data, PSK, CCMP_128);
        assert_eq!(ptk_result.is_ok(), true);

        // IEEE Std 802.11-2016, J.7.2, Table J-14
        let expected_kck = Vec::from_hex("379f9852d0199236b94e407ce4c00ec8").unwrap();
        let expected_kek = Vec::from_hex("47c9edc01c2c6e5b4910caddfb3e51a7").unwrap();
        let expected_tk = Vec::from_hex("b2360c79e9710fdd58bea93deaf06599").unwrap();
        let ptk = ptk_result.unwrap();
        assert_eq!(ptk.kck(), &expected_kck[..]);
        assert_eq!(ptk.kek(), &expected_kek[..]);
        assert_eq!(ptk.tk(), &expected_tk[..]);
    }

    // IEEE Std 802.11-2016, J.7.1 & J.7.3
    #[test]
    fn test_pairwise_key_hierarchy_tkip() {
        let data = ieee_test_data();
        let ptk_result = new_ptk(&data, PSK, TKIP);
        assert_eq!(ptk_result.is_ok(), true);

        // IEEE Std 802.11-2016, J.7.3, Table J-15
        let expected_kck = Vec::from_hex("379f9852d0199236b94e407ce4c00ec8").unwrap();
        let expected_kek = Vec::from_hex("47c9edc01c2c6e5b4910caddfb3e51a7").unwrap();
        let expected_tk = Vec::from_hex(
            "b2360c79e9710fdd58bea93deaf06599db980afbc29c152855740a6ce5ae3827",
        ).unwrap();
        let ptk = ptk_result.unwrap();
        assert_eq!(ptk.kck(), &expected_kck[..]);
        assert_eq!(ptk.kek(), &expected_kek[..]);
        assert_eq!(ptk.tk(), &expected_tk[..]);
    }

    #[test]
    fn test_pairwise_key_hierarchy_invalid_pmk() {
        let mut data = ieee_test_data();
        data.pmk.remove(0); // Invalidate PMK.
        let ptk_result = new_ptk(&data, PSK, CCMP_128);
        assert_eq!(ptk_result.is_err(), true);
    }

    #[test]
    fn test_pairwise_key_hierarchy_unsupported_akm() {
        let data = ieee_test_data();
        let ptk_result = new_ptk(&data, 200, CCMP_128);
        assert_eq!(ptk_result.is_err(), true);
    }

    #[test]
    fn test_pairwise_key_hierarchy_unsupported_cipher() {
        let data = ieee_test_data();
        let ptk_result = new_ptk(&data, PSK, 200);
        assert_eq!(ptk_result.is_err(), true);
    }
}
