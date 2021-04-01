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
/// A VBMeta structure to be read on startup for verified boot.
pub struct VBMeta {
    /// The raw bytes of VBMeta that can be written to the device image.
    pub bytes: Vec<u8>,
    _header: Header,
    _descriptors: Vec<HashDescriptor>,
    _key: Key,
}

impl VBMeta {
    /// Constructs a new VBMeta using the provided `descriptors` and AVB `key`.
    /// This can fail if signing with `key` failed.
    pub fn try_new(descriptors: Vec<HashDescriptor>, key: Key) -> Result<Self, SignFailure> {
        let mut header = Header::default();
        let aux_data = generate_aux_data(&mut header, &descriptors, &key);
        let auth_data = generate_auth_data(&mut header, &key, &aux_data)?;

        let mut bytes: Vec<u8> = Vec::new();
        bytes.extend_from_slice(header.as_bytes());
        bytes.extend_from_slice(&auth_data);
        bytes.extend_from_slice(&aux_data);

        Ok(VBMeta { bytes, _header: header, _descriptors: descriptors, _key: key })
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

#[test]
fn vbmeta() {
    use crate::descriptor::Salt;
    use crate::test;
    use std::convert::TryFrom;

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
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xD8,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0,

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
    let vbmeta_bytes = VBMeta::try_new(descriptors, key).unwrap().bytes;
    assert_eq!(vbmeta_bytes[..expected_header.len()], expected_header);
    test::hash_data_and_expect(
        &vbmeta_bytes,
        "295dad85e09205e0c9cb70ea313b4ddd4f959b3d25c4ff3606a9ff816634a240",
    );
}
