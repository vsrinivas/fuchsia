// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::byteorder::big_endian::{U32 as BigEndianU32, U64 as BigEndianU64};

use super::{HashDescriptor, HashDescriptorHeader, Salt, ALGORITHM, HASH_DESCRIPTOR_TAG};

/// A builder for `HashDescriptor` that is able to more flexibly create a HashDescriptor. This
/// can create HashDescriptors with omitted fields (e.g. one that has a name and size, but no
/// salt or digest).
#[derive(Debug, Default)]
pub struct RawHashDescriptorBuilder {
    name: Option<String>,
    size: Option<u64>,
    salt: Option<Salt>,
    digest: Option<[u8; 32]>,
    flags: Option<u32>,
    min_avb_version: Option<[u32; 2]>,
}

impl RawHashDescriptorBuilder {
    /// Provide the minimum avb version for the descriptor
    pub fn min_avb_version(self, min_avb_version: [u32; 2]) -> Self {
        Self { min_avb_version: Some(min_avb_version), ..self }
    }
    /// Provide the name field for the HashDescriptor being built.
    pub fn name(self, name: impl AsRef<str>) -> Self {
        Self { name: Some(name.as_ref().to_owned()), ..self }
    }
    /// Provide the size field for the HashDescriptor being built.
    pub fn size(self, size: u64) -> Self {
        Self { size: Some(size), ..self }
    }
    /// Provide the salt field for the HashDescriptor being built.
    pub fn salt(self, salt: Salt) -> Self {
        Self { salt: Some(salt), ..self }
    }
    /// Provide the digest field directly for the HashDescriptor being built.
    pub fn digest(self, digest: &[u8]) -> Self {
        let mut bytes = [0u8; 32];
        bytes.copy_from_slice(digest);

        Self { digest: Some(bytes), ..self }
    }

    /// Provide the flags field for the HashDescriptor being built.
    pub fn flags(self, flags: u32) -> Self {
        Self { flags: Some(flags), ..self }
    }

    /// Calculate the digest for the passed-in image, and use that to create
    /// a HashDescriptor from this builder.
    pub fn build_with_digest_calculated_for(self, image: &[u8]) -> HashDescriptor {
        let digest = HashDescriptor::calculate_digest_for_image(&self.salt, image);
        self.digest(&digest).build()
    }

    /// Create the HashDescriptor from this builder.
    pub fn build(self) -> HashDescriptor {
        // Build the header with the information we have (this may not be a
        // "useful" result, but it's what we've got).
        let image_name_size = self.name.as_ref().map_or(0, |name| name.len() as u32);
        let salt_size = match &self.salt {
            Some(_) => 32,
            _ => 0,
        };
        let digest_size = match &self.digest {
            Some(digest) => digest.len() as u32,
            _ => 0,
        };

        let header = HashDescriptorHeader {
            tag: HASH_DESCRIPTOR_TAG.into(),
            num_bytes_following: HashDescriptorHeader::calculate_num_bytes_following(
                image_name_size,
                salt_size,
                digest_size,
            )
            .into(),
            image_size: BigEndianU64::new(self.size.unwrap_or(0)),
            hash_alg: HashDescriptorHeader::bytes_for_algorithm(ALGORITHM),
            image_name_size: image_name_size.into(),
            salt_size: salt_size.into(),
            digest_size: digest_size.into(),
            flags: BigEndianU32::new(self.flags.unwrap_or(0)),
            pad: [0u8; 60],
        };

        HashDescriptor {
            header,
            image_name: self.name.unwrap_or("".to_owned()),
            salt: self.salt,
            digest: self.digest,
            min_avb_version: self.min_avb_version,
        }
    }
}
