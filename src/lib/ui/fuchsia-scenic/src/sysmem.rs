// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_composition::{BufferCollectionExportToken, BufferCollectionImportToken},
    fuchsia_zircon as zx,
};

// Pair of tokens to be used with Scenic Allocator FIDL protocol.
pub struct BufferCollectionTokenPair {
    pub export_token: BufferCollectionExportToken,
    pub import_token: BufferCollectionImportToken,
}

impl BufferCollectionTokenPair {
    pub fn new() -> BufferCollectionTokenPair {
        let (raw_export_token, raw_import_token) =
            zx::EventPair::create().expect("failed to create eventpair");
        BufferCollectionTokenPair {
            export_token: BufferCollectionExportToken { value: raw_export_token },
            import_token: BufferCollectionImportToken { value: raw_import_token },
        }
    }
}
