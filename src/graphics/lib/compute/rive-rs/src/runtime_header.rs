// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use crate::core::BinaryReader;

const FINGERPRINT: &'static [u8] = "RIVE".as_bytes();

/// Rive file runtime header. The header is fonud at the beginning of every
/// Rive runtime file, and begins with a specific 4-byte format: "RIVE".
/// This is followed by the major and minor version of Rive used to create
/// the file. Finally the owner and file ids are at the end of header; these
/// unsigned integers may be zero.
#[derive(Debug)]
pub struct RuntimeHeader {
    major_version: u32,
    #[allow(unused)]
    minor_version: u32,
    #[allow(unused)]
    file_id: u32,
    property_to_field_index: HashMap<u32, u32>,
}

impl RuntimeHeader {
    /// Reads the header from a binary buffer.
    pub fn read(reader: &mut BinaryReader<'_>) -> Option<Self> {
        if reader.read_bytes(FINGERPRINT.len())? != FINGERPRINT {
            return None;
        }

        let major_version = reader.read_var_u64()? as u32;
        let minor_version = reader.read_var_u64()? as u32;
        let file_id = reader.read_var_u64()? as u32;

        let mut property_keys = Vec::new();

        loop {
            let property_key = reader.read_var_u64()? as u32;

            if property_key == 0 {
                break;
            }

            property_keys.push(property_key);
        }

        let mut property_to_field_index = HashMap::new();
        let mut current_u32 = 0;
        let mut current_bit = 8;

        for property_key in property_keys {
            if current_bit == 8 {
                current_u32 = reader.read_u32()?;
                current_bit = 0;
            }

            let field_index = (current_u32 >> current_bit) & 3;
            property_to_field_index.insert(property_key, field_index);
            current_bit += 2;
        }

        Some(Self { major_version, minor_version, file_id, property_to_field_index })
    }

    pub fn major_version(&self) -> u32 {
        self.major_version
    }

    pub fn property_file_id(&self, property_key: u32) -> Option<u32> {
        self.property_to_field_index.get(&property_key).cloned()
    }
}
