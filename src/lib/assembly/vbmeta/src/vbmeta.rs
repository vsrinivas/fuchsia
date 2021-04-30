// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::descriptor::HashDescriptor;
use crate::header::Header;
use crate::key::{Key, SignFailure, SIGNATURE_SIZE};

use ring::digest;
use zerocopy::AsBytes;

const HASH_SIZE: u64 = 0x40;

#[derive(Debug)]
/// A struct for creating the VBMeta image to be read on startup for verified boot.
///
/// This holds both the completed image bytes and the header, descriptors, and
/// key used to create the vbmeta image, with accessors for each of them.
pub struct VBMeta {
    /// The raw bytes of VBMeta that can be written to the device image.
    bytes: Vec<u8>,

    /// The VBMeta header that was created.
    header: Header,

    /// The descriptors used to create the VBMeta
    descriptors: Vec<HashDescriptor>,

    /// The key used to sign the VBMeta
    key: Key,
}

impl VBMeta {
    /// Constructs and signs a new VBMeta image using the provided `descriptors` and AVB `key`.
    /// This can fail if signing with `key` failed.
    pub fn sign(descriptors: Vec<HashDescriptor>, key: Key) -> Result<Self, SignFailure> {
        let mut header = Header::default();

        // the minimum version in the header must be the minimum version required
        // by all HashDescriptors.
        if let Some(required_avb_version) =
            descriptors.iter().filter_map(|d| d.get_min_avb_version()).max()
        {
            header.min_avb_version_major = required_avb_version[0].into();
            header.min_avb_version_minor = required_avb_version[1].into();
        }

        let aux_data = generate_aux_data(&mut header, &descriptors, &key);
        let auth_data = generate_auth_data(&mut header, &key, &aux_data)?;

        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend_from_slice(header.as_bytes());
        bytes.extend_from_slice(&auth_data);
        bytes.extend_from_slice(&aux_data);

        Ok(VBMeta { bytes, header: header, descriptors: descriptors, key: key })
    }

    /// Returns an immutable byte slice containing the VBMeta image.
    pub fn as_bytes(&self) -> &[u8] {
        self.bytes.as_bytes()
    }

    /// Returns an immutable slice of the descriptors used to create the VBMeta image.
    pub fn descriptors(&self) -> &[HashDescriptor] {
        &self.descriptors
    }

    /// Returns an immutable reference to the header struct for the VBMeta image.
    pub fn header(&self) -> &Header {
        &self.header
    }

    /// Returns an immutable reference to the key used to sign the VBMeta image.
    pub fn key(&self) -> &Key {
        &self.key
    }
}

fn generate_aux_data(header: &mut Header, descriptors: &[HashDescriptor], key: &Key) -> Vec<u8> {
    let mut data: Vec<u8> = Vec::new();

    // Append the descriptors.
    for descriptor in descriptors {
        data.extend_from_slice(&descriptor.to_bytes());
    }
    header.descriptors_offset.set(0);
    header.descriptors_size.set(data.len() as u64);

    // Append the key.
    let key_header = key.generate_key_header();
    header.public_key_offset.set(data.len() as u64);
    header.public_key_size.set(key_header.len() as u64);
    data.extend_from_slice(&key_header);

    // Append the metadata.
    header.public_key_metadata_offset.set(data.len() as u64);
    header.public_key_metadata_size.set(key.metadata_bytes.len() as u64);
    data.extend_from_slice(&key.metadata_bytes);

    // Pad the aux data to the nearest 64 byte boundary.
    let length_with_padding = data.len() + 63 & !63;
    data.resize(length_with_padding, 0);
    header.aux_data_size.set(data.len() as u64);

    data
}

fn generate_auth_data(
    header: &mut Header,
    key: &Key,
    aux_data: &[u8],
) -> Result<Vec<u8>, SignFailure> {
    let mut data: Vec<u8> = Vec::new();

    // Set the remaining header values, which must be completed before hashing the header below.
    header.hash_offset.set(0);
    header.hash_size.set(HASH_SIZE);
    header.signature_offset.set(HASH_SIZE);
    header.signature_size.set(SIGNATURE_SIZE);
    header.auth_data_size.set(SIGNATURE_SIZE + HASH_SIZE);

    // Append the hash.
    let mut header_and_aux_data: Vec<u8> = Vec::new();
    header_and_aux_data.extend_from_slice(header.as_bytes());
    header_and_aux_data.extend_from_slice(&aux_data);
    let hash = digest::digest(&digest::SHA512, &header_and_aux_data);
    data.extend_from_slice(&hash.as_ref());

    // Append the signature.
    let signature = key.sign(&header_and_aux_data)?;
    data.extend_from_slice(&signature);

    Ok(data)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::descriptor::builder::RawHashDescriptorBuilder;
    use crate::descriptor::Salt;
    use crate::test;
    use std::convert::TryFrom;
    use zerocopy::LayoutVerified;

    #[test]
    fn simple_vbmeta() {
        #[rustfmt::skip]
        let expected_header = [
            // Magic: "AVB0"
            0x41, 0x56, 0x42, 0x30,

            // Minimum libavb version: 1.0
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,

            // Size of auth data: 0x240 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40,

            // Size of aux data: 0x500 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00,

            // Algorithm: 5 = sha256
            0x00, 0x00, 0x00, 0x05,

            // Section offsets/sizes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // hash_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, // hash_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, // signature_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, // signature_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, // public_key_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, // public_key_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD8, // public_key_metadata_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, // public_key_metadata_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // descriptors_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, // descriptors_size

            // Rollback index: 0
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

            // Flags: 0
            0x00, 0x00, 0x00, 0x00,

            // Rollback index location: 0
            0x00, 0x00, 0x00, 0x00,

            // Release string: "avbtool 1.2.0"
            0x61, 0x76, 0x62, 0x74, 0x6F, 0x6F, 0x6C, 0x20,
            0x31, 0x2E, 0x32, 0x2E, 0x30, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

            // Padding
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        ];

        let key = Key::try_new(test::TEST_PEM, test::TEST_METADATA).expect("new key");
        let salt = Salt::try_from(&[0xAA; 32][..]).expect("new salt");
        let descriptor = HashDescriptor::new("image_name", &[0xBB; 32], salt);
        let descriptors = vec![descriptor];
        let vbmeta_bytes = VBMeta::sign(descriptors, key).unwrap().bytes;
        assert_eq!(vbmeta_bytes[..expected_header.len()], expected_header);
        test::hash_data_and_expect(
            &vbmeta_bytes,
            "295dad85e09205e0c9cb70ea313b4ddd4f959b3d25c4ff3606a9ff816634a240",
        );
    }

    #[test]
    fn vbmeta_with_multiple_descriptors() {
        #[rustfmt::skip]
        let expected_header_bytes: [u8; 256] = [
            // Magic: "AVB0"
            0x41, 0x56, 0x42, 0x30,

            // Minimum libavb version: 1.2
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,

            // Size of auth data: 0x240 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40,

            // Size of aux data: 0x500 bytes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x80,

            // Algorithm: 5 = sha256
            0x00, 0x00, 0x00, 0x05,

            // Section offsets/sizes
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // hash_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, // hash_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, // signature_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, // signature_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x60, // public_key_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, // public_key_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x68, // public_key_metadata_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, // public_key_metadata_size
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // descriptors_offset
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x60, // descriptors_size

            // Rollback index: 0
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

            // Flags: 0
            0x00, 0x00, 0x00, 0x00,

            // Rollback index location: 0
            0x00, 0x00, 0x00, 0x00,

            // Release string: "avbtool 1.2.0"
            0x61, 0x76, 0x62, 0x74, 0x6F, 0x6F, 0x6C, 0x20,
            0x31, 0x2E, 0x32, 0x2E, 0x30, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

            // Padding
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        ];

        let key = Key::try_new(test::TEST_PEM, test::TEST_METADATA).expect("new key");
        let salt = Salt::try_from(&[0xAA; 32][..]).expect("new salt");
        let descriptor = HashDescriptor::new("image_name", &[0xBB; 32], salt);
        let extra_descriptor = RawHashDescriptorBuilder::default()
            .min_avb_version([1, 2])
            .name("other_image")
            .size(123456789)
            .flags(1)
            .build();
        let descriptors = vec![descriptor, extra_descriptor];
        let vbmeta = VBMeta::sign(descriptors, key).unwrap();
        let vbmeta_bytes = vbmeta.as_bytes();

        if vbmeta_bytes[..expected_header_bytes.len()] != expected_header_bytes {
            // the bytes didn't line up as expected, so compare the two header structs
            // directly, first, as it can have prettier results.
            let expected_header = LayoutVerified::<_, Header>::new(&expected_header_bytes as &[u8])
                .unwrap()
                .into_ref();
            assert_eq!(
                vbmeta.header(),
                expected_header,
                "generated header: {:#?}\nexpected header:{:#?}",
                vbmeta.header(),
                expected_header
            );
            // and a final assert in case the problem is in the serialization of the header.
            assert_eq!(vbmeta_bytes[..expected_header_bytes.len()], expected_header_bytes);
        }
        test::hash_data_and_expect(
            &vbmeta_bytes,
            "36e77362cc1bf8d1ca9680e135721d49287334ee323191c513feeb4f3d7a0774",
        );
    }
}
