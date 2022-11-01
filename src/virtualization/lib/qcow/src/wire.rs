// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Allow dead code here so we can retain some definitions from the spec that are not yet used.
#![allow(dead_code)]

use zerocopy::{AsBytes, FromBytes};

pub use zerocopy::byteorder::big_endian::{U32 as BE32, U64 as BE64};

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct Header {
    pub magic: BE32,
    pub version: BE32,
    pub backing_file_offset: BE64,
    pub backing_file_size: BE32,
    pub cluster_bits: BE32,
    pub size: BE64,
    pub crypt_method: BE32,
    pub l1_size: BE32,
    pub l1_table_offset: BE64,
    pub refcount_table_offset: BE64,
    pub nb_snapshots: BE32,
    pub snapshots_offset: BE64,
    pub incompatible_features: BE64,
    pub compatible_features: BE64,
    pub autoclear_features: BE64,
    pub refcount_order: BE32,
    pub header_length: BE32,
}

// Known values for ExtensionHeader::extension_type.
pub const QCOW_EXT_END: u32 = 0x00000000;
pub const QCOW_EXT_BACKING_FILE_FORMAT_NAME: u32 = 0xE2792AC;
pub const QCOW_EXT_FEATURE_NAME_TABLE: u32 = 0x6803f857;
pub const QCOW_EXT_BITMAPS: u32 = 0x23852875;

pub const QCOW_CRYPT_NONE: u32 = 0;
pub const QCOW_CRYPT_AES: u32 = 1;

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct ExtensionHeader {
    pub extension_type: BE32,
    pub extension_size: BE32,
}

#[derive(Default, Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct L1Entry(pub BE64);

const fn lut_entry_to_offset(entry: u64) -> Option<u64> {
    const QCOW_LUT_OFFSET_MASK: u64 = 0xfffffffffffe00;
    let offset = entry & QCOW_LUT_OFFSET_MASK;
    if offset == 0 {
        return None;
    }
    Some(offset)
}

impl L1Entry {
    pub fn offset(&self) -> Option<u64> {
        lut_entry_to_offset(self.0.get())
    }
}

#[derive(Default, Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct L2Entry(pub BE64);

impl L2Entry {
    pub fn compressed(&self) -> bool {
        const QCOW_L2_COMPRESSED: u64 = 1 << 62;
        self.0.get() & QCOW_L2_COMPRESSED != 0
    }

    pub fn offset(&self) -> Option<u64> {
        lut_entry_to_offset(self.0.get())
    }
}
