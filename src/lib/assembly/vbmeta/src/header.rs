// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zerocopy::{
    byteorder::network_endian::{U32, U64},
    AsBytes, FromBytes,
};

// Supported minimum platform AVB version.
const MIN_AVB_VERSION_MAJOR: u32 = 1;
const MIN_AVB_VERSION_MINOR: u32 = 0;

// AVB version of this library.
const AVB_VERSION_MAJOR: u32 = 1;
const AVB_VERSION_MINOR: u32 = 2;
const AVB_VERSION_SUB: u32 = 0;

const ALGORITHM: u32 = 5; // SHA256

fn get_release_string() -> String {
    format!("avbtool {}.{}.{}", AVB_VERSION_MAJOR, AVB_VERSION_MINOR, AVB_VERSION_SUB)
}

#[derive(AsBytes, FromBytes, Debug, PartialEq)]
#[repr(C, packed)]
#[allow(missing_docs)]
pub struct Header {
    magic: [u8; 4],
    pub min_avb_version_major: U32,
    pub min_avb_version_minor: U32,
    pub auth_data_size: U64,
    pub aux_data_size: U64,
    pub alg: U32,
    pub hash_offset: U64,
    pub hash_size: U64,
    pub signature_offset: U64,
    pub signature_size: U64,
    pub public_key_offset: U64,
    pub public_key_size: U64,
    pub public_key_metadata_offset: U64,
    pub public_key_metadata_size: U64,
    pub descriptors_offset: U64,
    pub descriptors_size: U64,
    pub rollback_index: U64,
    pub flags: U32,
    pub rollback_index_location: U32,
    release_string: [u8; 47],
    pad: [u8; 81],
}

impl Default for Header {
    fn default() -> Header {
        let release_string: String = get_release_string();
        let mut release_string_bytes = [0u8; 47];
        release_string_bytes[0..release_string.len()].copy_from_slice(release_string.as_bytes());

        Header {
            magic: *b"AVB0",
            min_avb_version_major: U32::new(MIN_AVB_VERSION_MAJOR),
            min_avb_version_minor: U32::new(MIN_AVB_VERSION_MINOR),
            auth_data_size: U64::new(0),
            aux_data_size: U64::new(0),
            alg: U32::new(ALGORITHM),
            hash_offset: U64::new(0),
            hash_size: U64::new(0),
            signature_offset: U64::new(0),
            signature_size: U64::new(0),
            public_key_offset: U64::new(0),
            public_key_size: U64::new(0),
            public_key_metadata_offset: U64::new(0),
            public_key_metadata_size: U64::new(0),
            descriptors_offset: U64::new(0),
            descriptors_size: U64::new(0),
            rollback_index: U64::new(0),
            flags: U32::new(0),
            rollback_index_location: U32::new(0),
            release_string: release_string_bytes,
            pad: [0u8; 81],
        }
    }
}
