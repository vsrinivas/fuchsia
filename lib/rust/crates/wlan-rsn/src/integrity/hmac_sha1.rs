// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crypto::hmac::Hmac;
use crypto::mac::{Mac, MacResult};
use crypto::sha1::Sha1;
use failure;
use integrity::Algorithm;

pub struct HmacSha1;

impl HmacSha1 {
    pub fn new() -> HmacSha1 {
        HmacSha1 {}
    }
}

impl Algorithm for HmacSha1 {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool {
        match self.compute(key, data) {
            Ok(mut code) => {
                code.resize(expected.len(), 0);
                MacResult::new_from_owned(code) == MacResult::new(expected)
            }
            _ => false,
        }
    }

    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, failure::Error> {
        let mut hmac = Hmac::new(Sha1::new(), key);
        let mut out = vec![0u8; hmac.output_bytes()];
        hmac.input(data);
        hmac.raw_result(&mut out);
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn run_test_case(key: &[u8], data: &[u8], expected_hex: &str) {
        let hmac_sha1_128 = HmacSha1 {};
        let result = hmac_sha1_128.compute(key, data);
        assert!(result.is_ok());

        let expected = Vec::from_hex(expected_hex).unwrap();
        assert_eq!(expected, result.unwrap());

        assert!(hmac_sha1_128.verify(key, data, &expected[..]));
    }

    // RFC 2202, Test Case 1
    #[test]
    fn test_test_case_1() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let data = "Hi There".as_bytes();
        let expected = "b617318655057264e28bc0b6fb378c8ef146be00";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 2
    #[test]
    fn test_test_case_2() {
        let key = "Jefe".as_bytes();
        let data = "what do ya want for nothing?".as_bytes();
        let expected = "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79";
        run_test_case(key, data, expected);
    }

    // RFC 2202, Test Case 3
    #[test]
    fn test_test_case_3() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data = Vec::from_hex("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd").unwrap();
        let expected = "125d7342b9ac11cd91a39af48aa17b4f63f175d3";
        run_test_case(&key[..], &data[..], expected);
    }

    // RFC 2202, Test Case 4
    #[test]
    fn test_test_case_4() {
        let key = Vec::from_hex("0102030405060708090a0b0c0d0e0f10111213141516171819").unwrap();
        let data = Vec::from_hex("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd").unwrap();
        let expected = "4c9007f4026250c6bc8414f9bf50c86c2d7235da";
        run_test_case(&key[..], &data[..], expected);
    }

    // RFC 2202, Test Case 5
    #[test]
    fn test_test_case_5() {
        let key = Vec::from_hex("0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c").unwrap();
        let data = "Test With Truncation".as_bytes();
        let expected = "4c1a03424b55e07fe7f27be1d58bb9324a9a5a04";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 6
    #[test]
    fn test_test_case_6() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data = "Test Using Larger Than Block-Size Key - Hash Key First".as_bytes();
        let expected = "aa4ae5e15272d00e95705637ce8a3b55ed402112";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 7
    #[test]
    fn test_test_case_7() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data =
            "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data".as_bytes();
        let expected = "e8e99d0f45237d786d6bbaa7965c7808bbff1a91";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 8
    #[test]
    fn test_test_case_8() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data = "Test Using Larger Than Block-Size Key - Hash Key First".as_bytes();
        let expected = "aa4ae5e15272d00e95705637ce8a3b55ed402112";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 9
    #[test]
    fn test_test_case_9() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data =
            "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data".as_bytes();
        let expected = "e8e99d0f45237d786d6bbaa7965c7808bbff1a91";
        run_test_case(&key[..], data, expected);
    }

    #[test]
    fn test_verify_failure() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let data = "Hi There".as_bytes();
        let expected = Vec::from_hex("b617318655057264e28bc0b6fb378c8ef146be00").unwrap();
        let hmac_sha1_128 = HmacSha1 {};
        let result = hmac_sha1_128.verify(&key[..], data, &expected[..]);
        assert!(result);

        let not_expected = Vec::from_hex("c617318655057264e28bc0b6fb378c8ef146be00").unwrap();
        let result = hmac_sha1_128.verify(&key[..], data, &not_expected[..]);
        assert!(!result);
    }
}
