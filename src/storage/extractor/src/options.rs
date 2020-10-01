// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::properties::DEFAULT_ALIGNMENT;

/// `ExtractorOptions` tells what types of extents should be extracted and
/// controls the contents of the extracted image.
#[repr(C)]
#[no_mangle]
#[derive(PartialEq, Debug, PartialOrd, Clone, Copy)]
pub struct ExtractorOptions {
    /// If `true`, forces dumping of blocks that are considered pii by the
    /// storage software. Enable this with caustion.
    pub force_dump_pii: bool,

    /// If `true`, each extent's checksums are added to extracted image.
    pub add_checksum: bool,

    /// Forces alignment of extents and extractor metadata within extracted
    /// image file.
    pub alignment: u64,
}

impl Default for ExtractorOptions {
    fn default() -> Self {
        Self { force_dump_pii: false, add_checksum: false, alignment: DEFAULT_ALIGNMENT }
    }
}
