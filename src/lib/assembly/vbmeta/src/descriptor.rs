// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::BigEndian;
use mundane::hash::{Digest, Hasher, Sha256};
use ring::{rand, rand::SecureRandom};
use std::convert::{TryFrom, TryInto};
use thiserror::Error;
use zerocopy::AsBytes;

const ALGORITHM: &str = "sha256";

type BigEndianU32 = zerocopy::U32<BigEndian>;
type BigEndianU64 = zerocopy::U64<BigEndian>;

/// A descriptor that contains the salt and digest of a provided image.
#[derive(Debug)]
pub struct HashDescriptor {
    header: HashDescriptorHeader,
    image_name: String,
    salt: Salt,
    digest: [u8; 32],
}

impl HashDescriptor {
    /// Construct a new HashDescriptor by hashing the provided `image`.
    pub fn new(image_name: &str, image: &[u8], salt: Salt) -> HashDescriptor {
        let header = HashDescriptorHeader::new(image_name, image);

        // Generate the digest with the salt prepended to the image.
        let mut salted_image: Vec<u8> = Vec::new();
        salted_image.extend_from_slice(&salt.bytes);
        salted_image.extend_from_slice(image);
        let digest = Sha256::hash(&salted_image).bytes();

        Self { header, image_name: image_name.to_string(), salt, digest }
    }

    /// Serialize the HashDescriptor in the format expected by VBMeta.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend_from_slice(self.header.as_bytes());
        bytes.extend_from_slice(self.image_name.as_bytes());
        bytes.extend_from_slice(&self.salt.bytes);
        bytes.extend_from_slice(&self.digest);

        // Pad to the nearest 8 byte boundary.
        let length_with_padding = bytes.len() + 7 & !7;
        bytes.resize(length_with_padding, 0);

        bytes
    }
}

#[derive(AsBytes, Debug)]
#[repr(C, packed)]
struct HashDescriptorHeader {
    tag: BigEndianU64,
    num_bytes_following: BigEndianU64,
    image_size: BigEndianU64,
    hash_alg: [u8; 32],
    image_name_size: BigEndianU32,
    salt_size: BigEndianU32,
    digest_size: BigEndianU32,
    flags: BigEndianU32,
    pad: [u8; 60],
}

impl HashDescriptorHeader {
    fn new(image_name: &str, image: &[u8]) -> HashDescriptorHeader {
        let algorithm_bytes = |algorithm: &str| -> [u8; 32] {
            let mut bytes = [0u8; 32];
            bytes[..algorithm.len()].copy_from_slice(algorithm.as_bytes());
            bytes
        };
        let alg_bytes = algorithm_bytes(ALGORITHM);

        HashDescriptorHeader {
            tag: BigEndianU64::new(2),
            num_bytes_following: BigEndianU64::new(0xC0),
            image_size: BigEndianU64::new(image.len() as u64),
            hash_alg: alg_bytes,
            image_name_size: BigEndianU32::new(image_name.len() as u32),
            salt_size: BigEndianU32::new(32),
            digest_size: BigEndianU32::new(32),
            flags: BigEndianU32::default(),
            pad: [0u8; 60],
        }
    }
}

/// Possible errors during salt construction.
#[derive(Error, Debug)]
pub enum SaltError {
    /// Salt failed to be randomly generated.
    #[error("random salt failed to generate")]
    GenerateRandom,
    /// Salt bytes input was an invalid size.
    #[error("salt is an invalid size")]
    InvalidSize,
}

/// Salt to be added before hashing the image.
#[derive(Debug)]
pub struct Salt {
    /// Raw bytes of the salt.
    pub bytes: [u8; 32],
}

impl TryFrom<&[u8]> for Salt {
    type Error = SaltError;
    fn try_from(bytes: &[u8]) -> Result<Salt, Self::Error> {
        match bytes.try_into() {
            Ok(bytes) => Ok(Self { bytes }),
            Err(_) => Err(Self::Error::InvalidSize),
        }
    }
}

impl Salt {
    /// Construct a random Salt.
    pub fn random() -> Result<Salt, SaltError> {
        let mut bytes = [0u8; 32];
        let rng = rand::SystemRandom::new();
        if rng.fill(&mut bytes).is_err() {
            return Err(SaltError::GenerateRandom);
        }
        Ok(Salt { bytes })
    }
}

#[cfg(test)]
mod tests {
    use crate::descriptor::HashDescriptor;
    use crate::descriptor::Salt;
    use std::convert::TryFrom;

    #[test]
    fn hash_descriptor() {
        let image_bytes = [0u8; 32];

        #[rustfmt::skip]
        let expected_bytes = [
            // tag: 2
            0, 0, 0, 0, 0, 0, 0, 0x02,

            // num bytes following: 0xC0
            0, 0, 0, 0, 0, 0, 0, 0xC0,

            // image size: 0x20
            0, 0, 0, 0, 0, 0, 0, 0x20,

            // hash algorithm name: sha256
            0x73, 0x68, 0x61, 0x32, 0x35, 0x36,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // image name size: 0x0A
            0, 0, 0, 0x0A,

            // salt size: 0x20
            0, 0, 0, 0x20,

            // digest size: 0x20
            0, 0, 0, 0x20,

            // flags: 0
            0, 0, 0, 0,

            // padding
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,

            // image name: "image_name"
            0x69, 0x6D, 0x61, 0x67, 0x65, 0x5F, 0x6E, 0x61, 0x6D, 0x65,

            // salt
            0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
            0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
            0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
            0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,

            // digest
            0x0A, 0xF8, 0xD9, 0x14, 0xBE, 0xB1, 0x05, 0x28,
            0x91, 0x7C, 0xE4, 0xDD, 0x74, 0x96, 0x23, 0xED,
            0x65, 0x09, 0x5C, 0x24, 0x64, 0xB2, 0x3F, 0x93,
            0x9F, 0x49, 0xEB, 0xA9, 0xE2, 0x1A, 0x1C, 0xA4,

            // padding
            0, 0,
        ];
        let salt_bytes = [0xAA; 32];
        let salt = Salt::try_from(&salt_bytes[..]).unwrap();
        let h = HashDescriptor::new("image_name", &image_bytes, salt);
        assert_eq!(h.to_bytes(), &expected_bytes);
    }

    #[test]
    fn salt_from_bytes() {
        let salt_bytes = [0u8; 32];
        let s = Salt::try_from(&salt_bytes[..]).unwrap();
        assert_eq!(&s.bytes, &salt_bytes);
    }

    #[test]
    fn salt_from_bytes_invalid_length() {
        let salt_bytes = [0u8; 33];
        let result = Salt::try_from(&salt_bytes[..]);
        assert!(result.is_err());
    }

    #[test]
    fn salt_random() {
        let s1 = Salt::random().unwrap();
        let s2 = Salt::random().unwrap();
        assert_ne!(s1.bytes, s2.bytes);
    }
}
