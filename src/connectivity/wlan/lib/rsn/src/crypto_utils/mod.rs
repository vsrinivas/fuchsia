// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod nonce;

// Used in PRF as specified in IEEE Std 802.11-2016, 12.7.1.2.
use crate::Error;
use anyhow::ensure;
use mundane::hash::Digest;
use mundane::hmac::HmacSha256;
#[allow(deprecated)]
use mundane::insecure::InsecureHmacSha1;

const VALID_PRF_BIT_SIZES: [usize; 6] = [128, 192, 256, 384, 512, 704];

// IEEE Std 802.11-2016, 12.7.1.2
// HMAC-SHA1 is considered insecure but is required to be used in IEEE 802.11's PRF.
#[allow(deprecated)]
pub(crate) fn prf(k: &[u8], a: &str, b: &[u8], bits: usize) -> Result<Vec<u8>, anyhow::Error> {
    ensure!(VALID_PRF_BIT_SIZES.contains(&bits), Error::InvalidBitSize(bits));

    let mut result = Vec::with_capacity(bits / 8);
    let iterations = (bits + 159) / 160;
    for i in 0..iterations {
        let mut hmac: InsecureHmacSha1 = InsecureHmacSha1::insecure_new(k);
        hmac.insecure_update(a.as_bytes());
        hmac.insecure_update(&[0u8]);
        hmac.insecure_update(b);
        hmac.insecure_update(&[i as u8]);
        result.extend_from_slice(&hmac.insecure_finish().bytes()[..]);
    }
    result.resize(bits / 8, 0);
    Ok(result)
}

// IEEE Std 802.11-2016, 12.7.1.7.2
// KDF-Hash-Length with Hash set to SHA256.
#[allow(unused)]
pub(crate) fn kdf_sha256(k: &[u8], label: &str, context: &[u8], bits: u16) -> Vec<u8> {
    let iterations: u16 = (bits + 255) / 256;
    let mut result = Vec::with_capacity((iterations * 256 / 8) as usize);
    for i in 1u16..=iterations {
        let mut hmac = HmacSha256::new(k);
        hmac.update(&i.to_le_bytes()[..]);
        hmac.update(label.as_bytes());
        hmac.update(context);
        hmac.update(&bits.to_le_bytes()[..]);
        result.extend_from_slice(&hmac.finish().bytes()[..]);
    }
    result.truncate((bits / 8) as usize);
    result
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
        let actual = prf(&key[..], "prefix", "what do ya want for nothing?".as_bytes(), 512);
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
        let actual = prf(&key[..], "prefix-2", "what do ya want for nothing?".as_bytes(), 256);
        assert_eq!(actual.is_ok(), true);

        let expected =
            Vec::from_hex("47c4908e30c947521ad20be9053450ecbea23d3aa604b77326d8b3825ff7475c")
                .unwrap();
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
        let actual = prf(&key[..], "something is happening", "Lorem ipsum".as_bytes(), 256);
        assert_eq!(actual.is_ok(), true);

        let expected =
            Vec::from_hex("5b154287399baeabd7d2c9682989e0933b3fdef8211ae7ae0c6586bb1e38de7c")
                .unwrap();
        assert_eq!(actual.unwrap(), expected);
    }

    #[test]
    fn test_prf_empty_prefix() {
        let key = Vec::from_hex("aaaa").unwrap();
        let actual = prf(&key[..], "", "Lorem ipsum".as_bytes(), 256);
        assert_eq!(actual.is_ok(), true);

        let expected =
            Vec::from_hex("1317523ae07f212fc4139ce9ebafe31ecf7c59cb07c7a7f04131afe7a59de60c")
                .unwrap();
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
            assert_eq!(result.is_ok(), true, "expected success with valid bit size: {:?}", *bits);
        }
    }

    #[test]
    fn test_prf_invalid_bit_sizes() {
        for bits in 0..2048_usize {
            if VALID_BIT_SIZES.contains(&bits) {
                continue;
            }
            let result = prf("".as_bytes(), "", "".as_bytes(), bits as usize);
            assert_eq!(result.is_err(), true, "expected failure with wrong bit size: {:?}", bits);
        }
    }

    // IEEE 802.11-18/1104r0: Deriving SAE password element. KDF-Hash-Length does not have published
    // test vectors, so we instead use examples from SAE test vectors.
    #[test]
    fn test_kdf_sha256_256() {
        let key = Vec::from_hex("b39919ad439eee7db61d9564421700acb9ed1044577ce9345e38c97de673c3f0")
            .unwrap();
        // IEEE Std 802.11-2016 12.4.4.2.2 defines this label
        let label = "SAE Hunting and Pecking";
        // This is the prime order of the 256-bit EC group specified by RFC 5903.
        let context =
            Vec::from_hex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff")
                .unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 256);
        let expected =
            Vec::from_hex("282167aa6ba380c53a9c5230c0b53b1ebc456983cc05f0f2b869a75f7f232e91")
                .unwrap();
        assert_eq!(hash, expected);
    }

    // IEEE 802.11-18/1104r0: Deriving SAE KCK and PMK.
    #[test]
    fn test_kdf_sha256_512() {
        let key = Vec::from_hex("7457a00754dcc4e3dc2850c124d6bb8fa1699d7fa33bb0667d9c34eeb513deb9")
            .unwrap();
        // IEEE Std 802.11-2016 12.4.5.4 defines this label
        let label = "SAE KCK and PMK";
        let context =
            Vec::from_hex("40a09b6017cebf0072843b5352aa2b4fb1a84a84c0fe5c235896466a2d3418b1")
                .unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 512);
        // This was computed by hand using the algorithm from IEEE Std 802.11-2016 12.4.5.4 and the
        // intermediate values from IEEE 802.11-18/1104r0.
        let expected = Vec::from_hex("599d6f1e27548be8499dceed2feccf94818ce1c79f1b4eb3d6a53228a09bf3ed7aead86fba4c3221fc437f5f14d70d854ea5d5aac1690116793081eda4d557c5").unwrap();
        assert_eq!(hash, expected);
    }

    #[test]
    fn test_kdf_sha256_short() {
        let key = Vec::from_hex("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = Vec::from_hex("babababa").unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 128);
        assert_eq!(hash.len(), 16);
    }

    #[test]
    fn test_kdf_sha256_empty_data() {
        let key = Vec::from_hex("f0f0f0f0").unwrap();
        let label = "LABELED!";
        let context = Vec::from_hex("babababa").unwrap();
        let hash = kdf_sha256(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }

    #[test]
    fn test_kdf_sha256_all_empty() {
        let key = vec![];
        let label = "";
        let context = vec![];
        let hash = kdf_sha256(&key[..], label, &context[..], 0);
        assert_eq!(hash.len(), 0);
    }
}
