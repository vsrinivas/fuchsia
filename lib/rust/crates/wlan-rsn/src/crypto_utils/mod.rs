// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod nonce;

use crypto::hmac::Hmac;
use crypto::mac::Mac;
use crypto::sha1::Sha1;
use {Error, Result};

const VALID_PRF_BIT_SIZES: [usize; 6] = [128, 192, 256, 384, 512, 704];

// IEEE Std 802.11-2016, 12.7.1.2
pub(crate) fn prf(k: &[u8], a: &str, b: &[u8], bits: usize) -> Result<Vec<u8>> {
    if !VALID_PRF_BIT_SIZES.contains(&bits) {
        Err(Error::InvalidBitSize(bits))
    } else {
        let mut result: Vec<u8> = Vec::with_capacity(bits / 8);

        let mut hmac = Hmac::new(Sha1::new(), k);
        let zero = [0u8];
        let iterations = (bits + 159) / 160;
        for i in 0..iterations {
            hmac.input(a.as_bytes());
            hmac.input(&zero[..]);
            hmac.input(b);
            hmac.input(&[i as u8][..]);
            result.extend_from_slice(hmac.result().code());
            hmac.reset();
        }
        result.resize(bits / 8, 0);
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    const VALID_BIT_SIZES: [usize; 6] = [128, 192, 256, 384, 512, 704];

    // IEEE Std 802.11-2016, J.3.2, Test case 1
    #[test]
    fn test_prf_test_case_1() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let actual = prf(&key[..], "prefix", "Hi There".as_bytes(), 512);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.3.2, Test case 2
    #[test]
    fn test_prf_test_case_2() {
        let key = "Jefe".as_bytes();
        let actual = prf(
            &key[..],
            "prefix",
            "what do ya want for nothing?".as_bytes(),
            512,
        );
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("51f4de5b33f249adf81aeb713a3c20f4fe631446fabdfa58244759ae58ef9009a99abf4eac2ca5fa87e692c440eb40023e7babb206d61de7b92f41529092b8fc").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.3.2, Test case 3
    #[test]
    fn test_prf_test_case_3() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data: [u8; 50] = [0xdd; 50];
        let actual = prf(&key[..], "prefix", &data[..], 512);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("e1ac546ec4cb636f9976487be5c86be17a0252ca5d8d8df12cfb0473525249ce9dd8d177ead710bc9b590547239107aef7b4abd43d87f0a68f1cbd9e2b6f7607").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.6.5, Test case 1
    #[test]
    fn test_prf_test_case_65_1() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let actual = prf(&key[..], "prefix", "Hi There".as_bytes(), 192);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.6.5, Test case 2
    #[test]
    fn test_prf_test_case_65_2() {
        let key = "Jefe".as_bytes();
        let actual = prf(
            &key[..],
            "prefix-2",
            "what do ya want for nothing?".as_bytes(),
            256,
        );
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex(
            "47c4908e30c947521ad20be9053450ecbea23d3aa604b77326d8b3825ff7475c",
        ).unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.6.5, Test case 3
    #[test]
    fn test_prf_test_case_65_3() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let actual = prf(
            &key[..],
            "prefix-3",
            "Test Using Larger Than Block-Size Key - Hash Key First".as_bytes(),
            384,
        );
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("0ab6c33ccf70d0d736f4b04c8a7373255511abc5073713163bd0b8c9eeb7e1956fa066820a73ddee3f6d3bd407e0682a").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    // IEEE Std 802.11-2016, J.6.5, Test case 4
    #[test]
    fn test_prf_test_case_65_4() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let actual = prf(&key[..], "prefix-4", "Hi There Again".as_bytes(), 512);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("248cfbc532ab38ffa483c8a2e40bf170eb542a2e0916d7bf6d97da2c4c5ca877736c53a65b03fa4b3745ce7613f6ad68e0e4a798b7cf691c96176fd634a59a49").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_empty_key() {
        let key: [u8; 0] = [];
        let actual = prf(
            &key[..],
            "something is happening",
            "Lorem ipsum".as_bytes(),
            256,
        );
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex(
            "5b154287399baeabd7d2c9682989e0933b3fdef8211ae7ae0c6586bb1e38de7c",
        ).unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_empty_prefix() {
        let key = Vec::from_hex("aaaa").unwrap();
        let actual = prf(&key[..], "", "Lorem ipsum".as_bytes(), 256);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex(
            "1317523ae07f212fc4139ce9ebafe31ecf7c59cb07c7a7f04131afe7a59de60c",
        ).unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_empty_data() {
        let key = Vec::from_hex("aaaa").unwrap();
        let actual = prf(&key[..], "some prefix", "".as_bytes(), 192);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("785e095774cfea480c267e74130cb86d1e3fc80095b66554").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_all_empty() {
        let key: [u8; 0] = [];
        let actual = prf(&key[..], "", "".as_bytes(), 128);
        assert_eq!(actual.is_ok(), true);

        let expected = Vec::from_hex("310354661a5962d5b8cb76032d5a97e8").unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_valid_bit_sizes() {
        for bits in &VALID_BIT_SIZES {
            let result = prf("".as_bytes(), "", "".as_bytes(), *bits as usize);
            assert_eq!(
                result.is_ok(),
                true,
                "expected success with valid bit size: {:?}",
                *bits
            );
        }
    }

    #[test]
    fn test_prf_invalid_bit_sizes() {
        for bits in 0..2048_usize {
            if VALID_BIT_SIZES.contains(&bits) {
                continue;
            }
            let result = prf("".as_bytes(), "", "".as_bytes(), bits as usize);
            assert_eq!(
                result.is_err(),
                true,
                "expected failure with wrong bit size: {:?}",
                bits
            );
        }
    }
}
