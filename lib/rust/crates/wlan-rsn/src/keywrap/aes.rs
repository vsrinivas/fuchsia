// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{BigEndian, ByteOrder};
use crypto::aes::{self, KeySize};
use crypto::blockmodes;
use crypto::buffer::{self, WriteBuffer, ReadBuffer};
use crypto::symmetriccipher::{BlockDecryptor, BlockEncryptor};
use keywrap::Algorithm;
use {Error, Result};

// Implementation of RFC 3394 - Advanced Encryption Standard (AES) Key Wrap Algorithm
// RFC 3394, 2.2.3
static DEFAULT_IV: &[u8] = &[0xa6; 8];
const BLOCK_SIZE: usize = 16;

pub struct NistAes;

impl NistAes {
    fn keysize(key_len: usize) -> Result<KeySize> {
        match key_len {
            16 => Ok(KeySize::KeySize128),
            24 => Ok(KeySize::KeySize192),
            32 => Ok(KeySize::KeySize256),
            _ => Err(Error::InvalidAesKeywrapKeySize(key_len)),
        }
    }
}

impl Algorithm for NistAes {
    // RFC 3394, 2.2.1 - Uses index based wrapping
    fn wrap(&self, key: &[u8], p: &[u8]) -> Result<Vec<u8>> {
        let n = p.len() / 8;
        if p.len() % 8 != 0 || n < 2 {
            return Err(Error::InvalidAesKeywrapDataLength(p.len()));
        }

        let keysize = NistAes::keysize(key.len())?;
        let mut b = vec![0u8; BLOCK_SIZE];

        // 1) Initialize variables
        // aes_block[:8] = A
        // aes_block[:] = A | R[i]
        let mut aes_block = vec![0u8; BLOCK_SIZE];
        &aes_block[..8].copy_from_slice(&DEFAULT_IV[..]);
        let mut c = vec![0u8; (n + 1) * 8];
        {
            let r = &mut c[8..];
            r.copy_from_slice(p);

            // 2) Calculate intermediate values
            for j in 0..6 {
                for i in 1..(n + 1) {
                    let ri = &mut r[(i - 1) * 8..(i * 8)];
                    &aes_block[8..16].copy_from_slice(ri);
                    {
                        let mut read_buf = buffer::RefReadBuffer::new(&aes_block[..]);
                        let mut write_buf = buffer::RefWriteBuffer::new(&mut b[..]);
                        let mut cipher = aes::ecb_encryptor(
                            keysize,
                            key,
                            blockmodes::NoPadding);
                        cipher.encrypt(&mut read_buf, &mut write_buf, true);
                    }
                    let t = (n * j + i) as u64;
                    BigEndian::write_u64(&mut aes_block, BigEndian::read_u64(&b[..8]) ^ t);

                    ri.copy_from_slice(&b[8..]);
                }
            }
        }

        // 3) Output the results
        let a = &aes_block[..8];
        &c[..8].copy_from_slice(a);
        Ok(c)
    }

    // RFC 3394, 2.2.2 - uses index based unwrapping
    fn unwrap(&self, key: &[u8], c: &[u8]) -> Result<Vec<u8>> {
        let n = c.len() / 8 - 1;
        if c.len() % 8 != 0 || n < 2 {
            return Err(Error::InvalidAesKeywrapDataLength(c.len()));
        }

        let keysize = NistAes::keysize(key.len())?;
        let mut b = vec![0u8; BLOCK_SIZE];

        // 1) Initialize variables
        // aes_block[:8] = A
        // aes_block[:] = (A ^ t) | R[i]
        let mut aes_block = vec![0u8; BLOCK_SIZE];
        &aes_block[..8].copy_from_slice(&c[..8]);
        let mut r = vec![0u8; n * 8];
        r.copy_from_slice(&c[8..]);

        // 2) Calculate intermediate values
        for j in (0..6).rev() {
            for i in (1..n + 1).rev() {
                let t = (n * j + i) as u64;
                let v = BigEndian::read_u64(&aes_block[..8]) ^ t;
                BigEndian::write_u64(&mut aes_block[..8], v);

                let ri = &mut r[(i - 1) * 8..i * 8];
                &aes_block[8..16].copy_from_slice(ri);
                {
                    let mut read_buf = buffer::RefReadBuffer::new(&aes_block[..]);
                    let mut write_buf = buffer::RefWriteBuffer::new(&mut b[..]);
                    let mut cipher = aes::ecb_decryptor(
                        keysize,
                        key,
                        blockmodes::NoPadding);
                    cipher.decrypt(&mut read_buf, &mut write_buf, true);
                }

                &aes_block[..8].copy_from_slice(&b[..8]);
                ri.copy_from_slice(&b[8..16]);
            }
        }

        // 3) Output the results
        if &aes_block[..8] != DEFAULT_IV {
            return Err(Error::WrongAesKeywrapKey);
        }
        Ok(r)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    fn test_wrap_unwrap<T: AsRef<[u8]>>(kek_hex: T, data_hex: T, expected_hex: T) {
        let kek = Vec::from_hex(kek_hex).unwrap();
        let data = Vec::from_hex(data_hex).unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap(&kek[..], &data[..]);
        assert_eq!(result.is_ok(), true);
        let expected = Vec::from_hex(expected_hex).unwrap();
        assert_eq!(result.unwrap(), expected);
        let plain = keywrap.unwrap(&kek[..], &expected[..]);
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
        let result = keywrap.wrap(&kek[..], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap(&kek[..], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_invalid_data_length() {
        let kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex(
            "012345678912345601234567891234560123456789123456012345678912345614",
        ).unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap(&kek[..], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap(&kek[..], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_unwrap_wrong_key() {
        let mut kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex("00112233445566778899AABBCCDDEEFF").unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap(&kek[..], &data[..]);
        assert_eq!(result.is_ok(), true);
        kek[0] = 0xFF;
        let plain = keywrap.unwrap(&kek[..], &data[..]);
        assert_eq!(plain.is_err(), true);
    }

    #[test]
    fn test_too_short_data() {
        let kek = Vec::from_hex("000102030405060708090A0B0C0D0E0F").unwrap();
        let data = Vec::from_hex("0011223344556677").unwrap();
        let keywrap = NistAes;
        let result = keywrap.wrap(&kek[..], &data[..]);
        assert_eq!(result.is_err(), true);
        let plain = keywrap.unwrap(&kek[..], &data[..]);
        assert_eq!(plain.is_err(), true);
    }
}