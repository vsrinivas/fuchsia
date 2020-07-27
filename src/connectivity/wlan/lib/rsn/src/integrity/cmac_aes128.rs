// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Algorithm;
use crate::Error;
use byteorder::{BigEndian, ByteOrder};
use crypto::aessafe;
use crypto::blockmodes::{EcbEncryptor, NoPadding};
use crypto::buffer;
use crypto::symmetriccipher::Encryptor;
use log::error;

/// The AES CMAC-128 algorithm is used for integrity checks on AKMS 00-0F-AC:3 through 00-0F-AC:9.
/// https://tools.ietf.org/html/rfc4493
pub struct CmacAes128;

impl CmacAes128 {
    pub fn new() -> Self {
        Self
    }
}

const BLOCK_SIZE: usize = 16;

impl Algorithm for CmacAes128 {
    fn verify(&self, key: &[u8], data: &[u8], expected: &[u8]) -> bool {
        match self.compute(key, data) {
            Ok(res) => res == expected,
            Err(e) => {
                error!("Error verifying message integrity: {:?}", e);
                false
            }
        }
    }

    fn compute(&self, key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
        let key_u128 = match key.len() {
            16 => BigEndian::read_u128(key),
            len => return Err(Error::InvalidKeyLength(len as u16, 16)),
        };
        let (k1, k2) = generate_subkeys(key_u128)?;
        let mut n = ((data.len() as f32) / BLOCK_SIZE as f32).ceil() as usize;
        let last_block_size = data.len() % BLOCK_SIZE;
        let last_block_complete = if n == 0 {
            n = 1;
            false
        } else {
            last_block_size == 0
        };

        let mut x = 0u128;
        for i in 0..n - 1 {
            let block_bytes = &data[i * BLOCK_SIZE..(i + 1) * BLOCK_SIZE];
            let block = BigEndian::read_u128(block_bytes);
            let y = x ^ block;
            x = BigEndian::read_u128(&encrypt_aes128(key, &y.to_be_bytes())?[..]);
        }

        let last_block = if last_block_complete {
            BigEndian::read_u128(&data[(n - 1) * BLOCK_SIZE..]) ^ k1
        } else {
            // Pad last block with a single one bit followed by trailing zeroes.
            let mut last_block = [0u8; BLOCK_SIZE];
            last_block[..last_block_size].copy_from_slice(&data[(n - 1) * BLOCK_SIZE..]);
            // We'll only end up here if the last block is partially filled, so this will never write past the end.
            last_block[last_block_size] = 0b10000000;
            BigEndian::read_u128(&last_block[..]) ^ k2
        };

        let y = last_block ^ x;
        encrypt_aes128(key, &y.to_be_bytes())
    }
}

/// Perform a single AES-128 ECB encryption
fn encrypt_aes128(key: &[u8], data: &[u8]) -> Result<Vec<u8>, Error> {
    let aes_enc = aessafe::AesSafe128Encryptor::new(key);
    let mut encryptor = EcbEncryptor::new(aes_enc, NoPadding);
    let mut out = vec![0u8; data.len()];
    let mut read_buf = buffer::RefReadBuffer::new(data);
    let mut write_buf = buffer::RefWriteBuffer::new(&mut out[..]);
    encryptor.encrypt(&mut read_buf, &mut write_buf, true).map_err(|e| {
        error!("Error performing AES-128 encryption: {:?}", e);
        Error::ComputingMicEncryptionError
    })?;
    Ok(out)
}

const RB: u128 = 0x87;

/// RFC 4493 Section 2.3
fn generate_subkeys(key: u128) -> Result<(u128, u128), Error> {
    let l = encrypt_aes128(&key.to_be_bytes()[..], &[0u8; 16][..])?;
    let l = BigEndian::read_u128(&l[..]);
    let k1 = match l & (1u128 << 127) {
        0 => l << 1, // MSB(L) is equal to 0
        _ => (l << 1) ^ RB,
    };
    let k2 = match k1 & (1u128 << 127) {
        0 => k1 << 1, // MSB(K1) is equal to 0
        _ => (k1 << 1) ^ RB,
    };
    Ok((k1, k2))
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;
    use wlan_common::assert_variant;

    const K: &str = "2b7e151628aed2a6abf7158809cf4f3c";

    // RFC 4493, 4. Test Vectors: Subkey Generation
    #[test]
    fn test_subkey_generation() {
        let k = BigEndian::read_u128(&Vec::from_hex(K).unwrap()[..]);
        let expected_k1 = 0xfbeed618357133667c85e08f7236a8de;
        let expected_k2 = 0xf7ddac306ae266ccf90bc11ee46d513b;
        let (k1, k2) = generate_subkeys(k).unwrap();
        assert_eq!(k1, expected_k1);
        assert_eq!(k2, expected_k2);
    }

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
            Err(Error::InvalidKeyLength(3, 16))
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
