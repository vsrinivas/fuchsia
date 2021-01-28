// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        aes::{self, SizedKey},
        integrity::Algorithm,
        Error,
    },
    log::error,
};

/// The AES CMAC-128 algorithm is used for integrity checks on AKMS 00-0F-AC:3 through 00-0F-AC:9.
/// https://tools.ietf.org/html/rfc4493
pub struct CmacAes128;

impl CmacAes128 {
    pub fn new() -> Self {
        Self
    }
}

impl Algorithm for CmacAes128 {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool {
        match self.compute(key, data) {
            Ok(output) => output == expected,
            Err(error) => {
                error!("message integrity verification failed: {:?}", error);
                false
            }
        }
    }

    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        let key = SizedKey::try_from_slice(key)?;
        let mac = aes::cmac(&key, data)?;
        Ok(mac.into())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::aes::AesError, hex::FromHex, wlan_common::assert_variant};

    const K: &str = "2b7e151628aed2a6abf7158809cf4f3c";

    // RFC 4493, 4. Test Vectors: Example 1
    #[test]
    fn test_len_0() {
        let key = Vec::from_hex(K).unwrap();
        let data = vec![];
        let expected = Vec::from_hex("bb1d6929e95937287fa37d129b756746").unwrap();
        let actual = CmacAes128::new().compute(&key[..], &data[..]).unwrap();
        assert_eq!(actual, expected);
    }

    // RFC 4493, 4. Test Vectors: Example 2
    #[test]
    fn test_len_16() {
        let key = Vec::from_hex(K).unwrap();
        let data = Vec::from_hex("6bc1bee22e409f96e93d7e117393172a").unwrap();
        let expected = Vec::from_hex("070a16b46b4d4144f79bdd9dd04a287c").unwrap();
        let actual = CmacAes128::new().compute(&key[..], &data[..]).unwrap();
        assert_eq!(actual, expected);
    }

    // RFC 4493, 4. Test Vectors: Example 3
    #[test]
    fn test_len_40() {
        let key = Vec::from_hex(K).unwrap();
        let data = Vec::from_hex(
            "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411",
        )
        .unwrap();
        let expected = Vec::from_hex("dfa66747de9ae63030ca32611497c827").unwrap();
        let actual = CmacAes128::new().compute(&key[..], &data[..]).unwrap();
        assert_eq!(actual, expected);
    }

    // RFC 4493, 4. Test Vectors: Example 4
    #[test]
    fn test_len_64() {
        let key = Vec::from_hex(K).unwrap();
        let data = Vec::from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710").unwrap();
        let expected = Vec::from_hex("51f0bebf7e3b9d92fc49741779363cfe").unwrap();
        let actual = CmacAes128::new().compute(&key[..], &data[..]).unwrap();
        assert_eq!(actual, expected);
    }

    #[test]
    fn test_wrong_key_len() {
        let key = Vec::from_hex("123456").unwrap();
        let data = vec![];
        assert_variant!(
            CmacAes128::new().compute(&key[..], &data[..]),
            Err(Error::Aes(AesError::KeySize(3)))
        );
    }

    #[test]
    fn test_verify_len_0() {
        let key = Vec::from_hex(K).unwrap();
        let data = vec![];
        let expected = Vec::from_hex("bb1d6929e95937287fa37d129b756746").unwrap();
        assert!(CmacAes128::new().verify(&key[..], &data[..], &expected[..]));
    }
}
