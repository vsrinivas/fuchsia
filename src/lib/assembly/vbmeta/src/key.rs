// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::BigEndian;
use modinv::modinv::inv_mod_u32;
use num::bigint::BigUint;
use num::traits::Pow;
use ring::signature::KeyPair;
use thiserror::Error;
use zerocopy::{byteorder::U32, AsBytes};

/// Number of bytes of a Signature.
pub const SIGNATURE_SIZE: u64 = 0x200;

/// Signature bytes returned by Key::sign().
pub type Signature = Vec<u8>;

fn calculate_rr(m: &BigUint, num_bits: u32) -> BigUint {
    let two_power_num_bits = BigUint::from(2u32).pow(num_bits);
    let rr: BigUint = two_power_num_bits.pow(2u8) % m;
    rr
}

fn calculate_n0inv(m: &BigUint) -> u32 {
    assert!(m.to_bytes_le().len() >= 4);
    let mut num_bytes = [0u8; 4];
    num_bytes[..].copy_from_slice(&m.to_bytes_le()[..4]);
    let num = u32::from_le_bytes(num_bytes);
    let n0inv = 0u32.wrapping_sub(inv_mod_u32(num));
    n0inv
}

#[derive(Debug)]
/// An AVB key used for creating signatures.
pub struct Key {
    /// The AVB key metadata raw bytes, which are written directly into the VBMeta blob.
    pub metadata_bytes: Vec<u8>,
    rsa: ring::signature::RsaKeyPair,
}

/// Errors that can occur during construction of a Key when parsing the PEM file.
#[derive(Error, Debug)]
pub enum KeyError {
    /// The PEM cannot be converted to a DER.
    #[error("cannot parse the pem")]
    ParsePem(#[from] pem::PemError),
    /// The DER cannot be converted to a RSA key pair.
    #[error("cannot parse the der")]
    ParseDer,
}

#[derive(Error, Debug)]
#[error("failed to sign")]
/// Signing with the key failed.
pub struct SignFailure;

impl From<ring::error::Unspecified> for SignFailure {
    fn from(_err: ring::error::Unspecified) -> Self {
        SignFailure
    }
}

impl Key {
    /// Construct a new Key using the provided PEM string and metadata bytes.
    /// This method can fail if the PEM or contained DER cannot be parsed.
    pub fn try_new<M: Into<Vec<u8>>>(pem_str: &str, metadata: M) -> Result<Key, KeyError> {
        let pem = pem::parse(pem_str)?;
        let rsa_result = ring::signature::RsaKeyPair::from_pkcs8(&pem.contents);
        if rsa_result.is_err() {
            return Err(KeyError::ParseDer);
        }
        let rsa = rsa_result.unwrap();
        Ok(Key { rsa, metadata_bytes: metadata.into() })
    }

    /// Sign `data` with the owned RSA key pair and return the Signature.
    pub fn sign(&self, data: &[u8]) -> Result<Signature, SignFailure> {
        let mut out = vec![0u8; SIGNATURE_SIZE as usize];
        let rng = ring::rand::SystemRandom::new();
        self.rsa.sign(&ring::signature::RSA_PKCS1_SHA512, &rng, data, &mut out)?;
        Ok(out)
    }

    /// Generate the AVB key header to be written directly into the VBMeta blob.
    pub fn generate_key_header(&self) -> Vec<u8> {
        let num_bits: u32 = (self.rsa.public_modulus_len() * 8usize) as u32;
        let num_bits_big_endian = U32::<BigEndian>::new(num_bits);
        let modulus = BigUint::from_bytes_be(
            self.rsa.public_key().modulus().big_endian_without_leading_zero(),
        );

        let rr = calculate_rr(&modulus, num_bits);
        let n0inv = calculate_n0inv(&modulus);

        // Assemble the bytes.
        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend_from_slice(num_bits_big_endian.as_bytes());
        bytes.extend_from_slice(U32::<BigEndian>::new(n0inv).as_bytes());
        bytes.extend_from_slice(&modulus.to_bytes_be());
        bytes.extend_from_slice(&rr.to_bytes_be());
        bytes
    }

    /// Returns a reference to the public portion of the key.
    pub fn public_key(&self) -> &ring::signature::RsaSubjectPublicKey {
        self.rsa.public_key()
    }
}

#[cfg(test)]
mod tests {
    use crate::key::Key;
    use crate::test;

    #[test]
    fn invalid_key() {
        let key = Key::try_new("Real Fake Doors", test::TEST_METADATA);
        assert!(key.is_err());
    }

    #[test]
    fn sign() {
        let key = Key::try_new(&test::TEST_PEM, test::TEST_METADATA).expect("new key");
        let signature = key.sign(&[0xBB; 32]).unwrap();
        test::hash_data_and_expect(
            &signature,
            "ded81c057368b9a27a54505b38c09b3278ab9630da5fcb88e810f5433001427a",
        );
    }

    #[test]
    fn generated_header_and_metadata() {
        let key = Key::try_new(&test::TEST_PEM, test::TEST_METADATA).expect("new key");
        assert_eq!(key.metadata_bytes, test::TEST_METADATA);
        test::hash_data_and_expect(
            &key.generate_key_header(),
            "aac7f1dea707ee029aa5f6d29337399454aa37d1d403a5dd743c1e4c4f6efae2",
        );
    }
}
