// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{integrity::Algorithm, Error},
    mundane::{hash::Digest, insecure::InsecureHmacMd5},
};

pub struct HmacMd5;

impl HmacMd5 {
    pub fn new() -> Self {
        Self
    }
}

impl Algorithm for HmacMd5 {
    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        let mut hmac = InsecureHmacMd5::insecure_new(key);
        hmac.insecure_update(data);
        Ok(hmac.insecure_finish().bytes().into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn run_test_case(key: &[u8], data: &[u8], expected_hex: &str) {
        let hmac_md5 = HmacMd5 {};
        let result = hmac_md5.compute(key, data);
        assert!(result.is_ok());

        let expected = Vec::from_hex(expected_hex).unwrap();
        assert_eq!(expected, result.unwrap());

        assert!(hmac_md5.verify(key, data, &expected[..]));
    }

    // RFC 2202, Test Case 1
    #[test]
    fn test_test_case_1() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let data = "Hi There".as_bytes();
        let expected = "9294727a3638bb1c13f48ef8158bfc9d";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 2
    #[test]
    fn test_test_case_2() {
        let key = "Jefe".as_bytes();
        let data = "what do ya want for nothing?".as_bytes();
        let expected = "750c783e6ab0b503eaa86e310a5db738";
        run_test_case(key, data, expected);
    }

    // RFC 2202, Test Case 3
    #[test]
    fn test_test_case_3() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data = Vec::from_hex("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd").unwrap();
        let expected = "56be34521d144c88dbb8c733f0e8b3f6";
        run_test_case(&key[..], &data[..], expected);
    }

    // RFC 2202, Test Case 4
    #[test]
    fn test_test_case_4() {
        let key = Vec::from_hex("0102030405060708090a0b0c0d0e0f10111213141516171819").unwrap();
        let data = Vec::from_hex("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd").unwrap();
        let expected = "697eaf0aca3a3aea3a75164746ffaa79";
        run_test_case(&key[..], &data[..], expected);
    }

    // RFC 2202, Test Case 5
    #[test]
    fn test_test_case_5() {
        let key = Vec::from_hex("0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c").unwrap();
        let data = "Test With Truncation".as_bytes();
        let expected = "56461ef2342edc00f9bab995690efd4c";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 6
    #[test]
    fn test_test_case_6() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data = "Test Using Larger Than Block-Size Key - Hash Key First".as_bytes();
        let expected = "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd";
        run_test_case(&key[..], data, expected);
    }

    // RFC 2202, Test Case 7
    #[test]
    fn test_test_case_7() {
        let key = Vec::from_hex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").unwrap();
        let data =
            "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data".as_bytes();
        let expected = "6f630fad67cda0ee1fb1f562db3aa53e";
        run_test_case(&key[..], data, expected);
    }

    #[test]
    fn test_verify_failure() {
        let key = Vec::from_hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b").unwrap();
        let data = "Hi There".as_bytes();
        let expected = Vec::from_hex("9294727a3638bb1c13f48ef8158bfc9d").unwrap();
        let hmac_md5 = HmacMd5 {};
        let result = hmac_md5.verify(&key[..], data, &expected[..]);
        assert!(result);
        let not_expected = Vec::from_hex("deadbeef3638bb1c13f48ef8158bfc9d").unwrap();
        let result = hmac_md5.verify(&key[..], data, &not_expected[..]);
        assert!(!result);
    }
}
