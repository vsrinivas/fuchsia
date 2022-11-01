// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use mundane::hash::{Digest, Hasher, Sha256};
use ring::{rand, rand::SecureRandom};
use serde::Deserialize;
use std::convert::{TryFrom, TryInto};
use thiserror::Error;
use zerocopy::{
    byteorder::big_endian::{U32 as BigEndianU32, U64 as BigEndianU64},
    AsBytes,
};

pub mod builder;

const ALGORITHM: &str = "sha256";

const HASH_DESCRIPTOR_TAG: u64 = 2;

/// A descriptor that contains the salt and digest of a provided image.
#[derive(Debug, PartialEq)]
pub struct HashDescriptor {
    header: HashDescriptorHeader,
    image_name: String,
    salt: Option<Salt>,
    digest: Option<[u8; 32]>,
    min_avb_version: Option<[u32; 2]>,
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

        Self {
            header,
            image_name: image_name.to_string(),
            salt: Some(salt),
            digest: Some(digest),
            min_avb_version: None,
        }
    }

    /// Serialize the HashDescriptor in the format expected by VBMeta.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend_from_slice(self.header.as_bytes());
        bytes.extend_from_slice(self.image_name.as_bytes());
        if let Some(salt) = &self.salt {
            bytes.extend_from_slice(&salt.bytes);
        }
        if let Some(digest) = &self.digest {
            bytes.extend_from_slice(digest);
        }

        // Pad to the nearest 8 byte boundary.
        let length_with_padding = bytes.len() + 7 & !7;
        bytes.resize(length_with_padding, 0);

        bytes
    }

    /// Accessor for the name of the image described by this Descriptor.
    pub fn image_name(&self) -> &str {
        &self.image_name
    }

    /// Accessor for the size of the image described by this Descriptor.
    pub fn image_size(&self) -> u64 {
        self.header.image_size.into()
    }

    /// Accessor for the salt used in the calculation of the digest.
    pub fn salt(&self) -> Option<Salt> {
        self.salt.clone()
    }

    /// Accessor for the digest of the image described by this Descriptor.
    pub fn digest(&self) -> Option<&[u8]> {
        match &self.digest {
            Some(d) => Some(&d[..]),
            _ => None,
        }
    }

    /// Accessor for the flags that are set on the Descriptor.
    pub fn flags(&self) -> u32 {
        self.header.flags.into()
    }

    /// Accessor for the minimum avb version that this HashDescriptor requires.
    pub fn get_min_avb_version(&self) -> Option<[u32; 2]> {
        self.min_avb_version
    }

    /// Calculate the digest for a given image, with an optional salt.
    pub fn calculate_digest_for_image(salt: &Option<Salt>, image: &[u8]) -> [u8; 32] {
        let mut image_to_hash: Vec<u8> = Vec::new();
        if let Some(salt) = salt {
            image_to_hash.extend_from_slice(&salt.bytes);
        }
        image_to_hash.extend_from_slice(image);
        Sha256::hash(&image_to_hash).bytes()
    }
}

#[derive(AsBytes, Debug, PartialEq)]
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
    /// Create a new HashDescriptorHeader based on a name and an image.
    fn new(image_name: &str, image: &[u8]) -> HashDescriptorHeader {
        let alg_bytes = Self::bytes_for_algorithm(ALGORITHM);

        let image_name_size = image_name.len() as u32;
        let salt_size = 32;
        let digest_size = 32;

        HashDescriptorHeader {
            tag: HASH_DESCRIPTOR_TAG.into(),
            num_bytes_following: Self::calculate_num_bytes_following(
                image_name_size,
                salt_size,
                digest_size,
            )
            .into(),
            image_size: BigEndianU64::new(image.len() as u64),
            hash_alg: alg_bytes,
            image_name_size: BigEndianU32::new(image_name_size),
            salt_size: BigEndianU32::new(salt_size),
            digest_size: BigEndianU32::new(digest_size),
            flags: BigEndianU32::default(),
            pad: [0u8; 60],
        }
    }

    /// Helper function which calculates the byte array that contains the name
    /// of the algorithm in the header.
    fn bytes_for_algorithm(algorithm: &str) -> [u8; 32] {
        let mut bytes = [0u8; 32];
        bytes[..algorithm.len()].copy_from_slice(algorithm.as_bytes());
        bytes
    }

    /// The `num_bytes_following` field is calculated based on the lengths of
    /// of the rest of the header struct after it, and the variable-length
    /// fields which are also appended to the header.
    ///
    /// That total size is then rounded up to the nearest multiple of 8 for
    /// alignment of the next descriptor in the vbmeta image.
    fn calculate_num_bytes_following(
        image_name_size: u32,
        salt_size: u32,
        digest_size: u32,
    ) -> u64 {
        let size = (std::mem::size_of::<HashDescriptorHeader>() // entire struct
            - std::mem::size_of::<u64>()    // `tag` field
            - std::mem::size_of::<u64>()    // `num_bytes` field
            + image_name_size as usize      // fields that are appended after the fixed-size struct
            + salt_size as usize
            + digest_size as usize) as u64;
        // adding 7 will push it up above the next multiple of 8, unless it's already a multiple of
        // 8.  Then clear the lowest 3 bits to make it an even multiple of 8.
        let rounded = (size + 7) & !7u64;
        rounded
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
    /// Salt input was in an invalid format.
    #[error("salt bytes are in an invalid format")]
    InvalidFormat,
}

/// Salt to be added before hashing the image.
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct Salt {
    /// Raw bytes of the salt.
    pub bytes: [u8; 32],
}

impl TryFrom<&[u8]> for Salt {
    type Error = SaltError;
    fn try_from(bytes: &[u8]) -> Result<Salt, Self::Error> {
        match bytes.try_into() {
            Ok(bytes) => Ok(Self { bytes }),
            Err(_) => Err(SaltError::InvalidSize),
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

    /// Decode a hex string to use as the value for the Salt.
    pub fn decode_hex(string: &str) -> Result<Self, SaltError> {
        if let Ok(byte_vec) = hex::decode(string) {
            Self::try_from(byte_vec.as_slice())
        } else {
            Err(SaltError::InvalidFormat)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::descriptor::builder::RawHashDescriptorBuilder;
    use assert_matches::assert_matches;
    use std::convert::TryFrom;

    /// Holds the set of inputs for a test.
    struct TestInputs {
        image_bytes: Vec<u8>,
        image_name: String,
        salt: Salt,
    }

    /// Holds a set of expected outputs for a test.
    struct ExpectedOutputs {
        bytes: Vec<u8>,
    }

    /// Helper fn to create the "simple" test inputs / outputs
    fn simple_test_setup() -> (TestInputs, ExpectedOutputs) {
        (
            TestInputs {
                image_bytes: vec![0u8; 32],
                image_name: "image_name".to_owned(),
                salt: Salt::try_from(&[0xAAu8; 32][..]).unwrap(),
            },
            ExpectedOutputs {
                #[rustfmt::skip]
                bytes: vec![
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
                ],
            },
        )
    }

    #[test]
    fn test_simple_hash_descriptor() {
        let (inputs, outputs) = simple_test_setup();

        let h = HashDescriptor::new(&inputs.image_name, &inputs.image_bytes, inputs.salt);
        assert_eq!(h.to_bytes(), outputs.bytes.as_slice());
    }

    #[test]
    fn test_simple_hash_descriptor_from_raw_builder() {
        let (inputs, outputs) = simple_test_setup();

        let h = RawHashDescriptorBuilder::default()
            .name(&inputs.image_name)
            .size(inputs.image_bytes.len() as u64)
            .salt(inputs.salt)
            .build_with_digest_calculated_for(&inputs.image_bytes);
        assert_eq!(h.to_bytes(), outputs.bytes.as_slice());
    }

    /// This tests a specific sort of hash descriptor that is different from the
    /// usual kind, and requires the RawHashDescriptorBuilder to create.
    #[test]
    fn hash_descriptor_without_digest() {
        let image_bytes = [0u8; 32];

        #[rustfmt::skip]
        let expected_bytes = [
            // tag: 2
            0, 0, 0, 0, 0, 0, 0, 0x02,

            // num bytes following: 0x80
            0, 0, 0, 0, 0, 0, 0, 0x80,

            // image size: 0x20
            0, 0, 0, 0, 0, 0, 0, 0x20,

            // hash algorithm name: sha256
            0x73, 0x68, 0x61, 0x32, 0x35, 0x36,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            // image name size: 0x0A
            0, 0, 0, 0x0A,

            // salt size: 0x00
            0, 0, 0, 0,

            // digest size: 0x00
            0, 0, 0, 0,

            // flags: 1
            0, 0, 0, 1,

            // padding
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0,

            // image name: "image_name"
            0x69, 0x6D, 0x61, 0x67, 0x65, 0x5F, 0x6E, 0x61, 0x6D, 0x65,

            // salt

            // digest

            // padding
            0, 0,
        ];
        let h = RawHashDescriptorBuilder::default()
            .name("image_name")
            .size(image_bytes.len() as u64)
            .flags(1)
            .build();
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
        assert_matches!(result, Err(SaltError::InvalidSize));
    }

    #[test]
    fn salt_from_str() {
        let salt_str = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
        let s = Salt::decode_hex(salt_str).unwrap();

        #[rustfmt::skip]
        assert_eq!(
            &s.bytes,
            &[
                0x01, 0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                0x01, 0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                0x01, 0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                0x01, 0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
            ]);
    }

    #[test]
    fn salt_from_str_invalid_format() {
        let salt_str = "this is not hex";
        let result = Salt::decode_hex(salt_str);
        assert_matches!(result, Err(SaltError::InvalidFormat));
    }

    #[test]
    fn salt_random() {
        let s1 = Salt::random().unwrap();
        let s2 = Salt::random().unwrap();
        assert_ne!(s1.bytes, s2.bytes);
    }
}
