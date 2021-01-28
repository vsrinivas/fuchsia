// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    aes::{self, KeyUnwrapIo, KeyWrapIo, SizedKey},
    keywrap::Algorithm,
    Error,
};

/// RFC 3394 AES key wrapping.
pub struct NistAes;

impl Algorithm for NistAes {
    /// RFC 3394, 2.2.1 AES key wrapping.
    // NOTE: The IV is never used in this implementation.
    fn wrap_key(&self, key: &[u8], _iv: &[u8; 16], p: &[u8]) -> Result<Vec<u8>, Error> {
        let key = SizedKey::try_from_slice(key)?;
        let kio = KeyWrapIo::try_from_input(p)?;
        let wrapped = aes::wrap_key(&key, None, kio)?;
        Ok(wrapped)
    }

    /// RFC 3394, 2.2.2 AES key unwrapping.
    // NOTE: The IV is never used in this implementation.
    fn unwrap_key(&self, key: &[u8], _iv: &[u8; 16], c: &[u8]) -> Result<Vec<u8>, Error> {
        let key = SizedKey::try_from_slice(key)?;
        let kio = KeyUnwrapIo::try_from_input(c)?;
        let unwrapped = aes::unwrap_key(&key, None, kio)?;
        Ok(unwrapped)
    }
}

// TODO: Move some of these tests into the `rsn::aes` module.
#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn test_wrap_unwrap<T: AsRef<[u8]>>(kek_hex: T, data_hex: T, expected_hex: T) {
        let kek = Vec::from_hex(kek_hex).unwrap();
        let data = Vec::from_hex(data_hex).unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &[0u8; 16], &data[..]);
        assert_eq!(result.is_ok(), true);
        let expected = Vec::from_hex(expected_hex).unwrap();
        assert_eq!(result.unwrap(), expected);
        let plain = keywrap.unwrap_key(&kek[..], &[0u8; 16], &expected[..]);
        assert_eq!(plain.is_ok(), true);
        assert_eq!(data, plain.unwrap());
    }

    // RFC 3394, 4.1 Wrap 128 bits of Key Data with a 128-bit KEK
    #[test]
    fn test_128_data_128_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F",
            "00112233445566778899AABBCCDDEEFF",
            "1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5",
        );
    }

    // RFC 3394, 4.2 Wrap 128 bits of Key Data with a 192-bit KEK
    #[test]
    fn test_128_data_192_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F1011121314151617",
            "00112233445566778899AABBCCDDEEFF",
            "96778B25AE6CA435F92B5B97C050AED2468AB8A17AD84E5D",
        );
    }

    // RFC 3394, 4.3 Wrap 128 bits of Key Data with a 256-bit KEK
    #[test]
    fn test_128_data_256_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
            "00112233445566778899AABBCCDDEEFF",
            "64E8C3F9CE0F5BA263E9777905818A2A93C8191E7D6E8AE7",
        );
    }
    // RFC 3394, 4.4 Wrap 192 bits of Key Data with a 192-bit KEK
    #[test]
    fn test_192_data_192_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F1011121314151617",
            "00112233445566778899AABBCCDDEEFF0001020304050607",
            "031D33264E15D33268F24EC260743EDCE1C6C7DDEE725A936BA814915C6762D2",
        );
    }

    // RFC 3394, 4.5 Wrap 192 bits of Key Data with a 256-bit KEK
    #[test]
    fn test_192_data_256_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
            "00112233445566778899AABBCCDDEEFF0001020304050607",
            "A8F9BC1612C68B3FF6E6F4FBE30E71E4769C8B80A32CB8958CD5D17D6B254DA1",
        );
    }

    // RFC 3394, 4.6 Wrap 256 bits of Key Data with a 256-bit KEK
    #[test]
    fn test_256_data_256_kek() {
        test_wrap_unwrap(
            "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F",
            "00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F",
            "28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21",
        );
    }

    #[test]
    fn test_invalid_key_length() {
        let kek = Vec::from_hex("0102030405060708090A0B0C0D0E0F").unwrap(); // 240bit
        let data = Vec::from_hex("00112233445566778899AABBCCDDEEFF").unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_invalid_data_length() {
        let kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data =
            Vec::from_hex("012345678912345601234567891234560123456789123456012345678912345614")
                .unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_unwrap_wrong_key() {
        let mut kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex("00112233445566778899AABBCCDDEEFF").unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(result.is_ok(), true);
        kek[0] = 0xFF;
        let plain = keywrap.unwrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_too_short_data() {
        let kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex("0011223344556677").unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap_key(&kek[..], &[0; 16], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_iv_unused() {
        let kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex("00112233445566778899AABBCCDDEEFF").unwrap();
        let iv = [0xab; 16];
        let keywrap = NistAes;
        let result = keywrap.wrap_key(&kek[..], &iv, &data[..]);
        assert_eq!(result.is_ok(), true);
        let expected = Vec::from_hex("1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5").unwrap();
        assert_eq!(result.unwrap(), expected);
        let plain = keywrap.unwrap_key(&kek[..], &iv, &expected[..]);
        assert_eq!(plain.is_ok(), true);
        assert_eq!(data, plain.unwrap());
    }
}
