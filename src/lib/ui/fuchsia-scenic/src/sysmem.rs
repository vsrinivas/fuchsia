// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, ClientEnd},
    fidl_fuchsia_sysmem as fsysmem, fidl_fuchsia_ui_composition as fland,
    fuchsia_zircon::{self as zx, AsHandleRef},
};

// Pair of tokens to be used with Scenic Allocator FIDL protocol.
pub struct BufferCollectionTokenPair {
    pub export_token: fland::BufferCollectionExportToken,
    pub import_token: fland::BufferCollectionImportToken,
}

impl BufferCollectionTokenPair {
    pub fn new() -> BufferCollectionTokenPair {
        let (raw_export_token, raw_import_token) =
            zx::EventPair::create().expect("failed to create eventpair");
        BufferCollectionTokenPair {
            export_token: fland::BufferCollectionExportToken { value: raw_export_token },
            import_token: fland::BufferCollectionImportToken { value: raw_import_token },
        }
    }
}

/// Given a Scenic `BufferCollectionImportToken`, returns a new version which has been duplicated.
pub fn duplicate_buffer_collection_import_token(
    import_token: &fland::BufferCollectionImportToken,
) -> Result<fland::BufferCollectionImportToken, Error> {
    let handle = import_token.value.as_handle_ref().duplicate(zx::Rights::SAME_RIGHTS)?;
    Ok(fland::BufferCollectionImportToken { value: handle.into() })
}

/// Calls `BufferCollectionToken.Duplicate()` on the provided token, passing the server end of a
/// newly-instantiated channel.  Then, calls `Sync()` on the provided token, so that the returned
/// token is safe to use immediately (i.e. the server has acknowledged that the duplication has
/// occurred).
pub async fn duplicate_buffer_collection_token(
    token: &mut fsysmem::BufferCollectionTokenProxy,
) -> Result<ClientEnd<fsysmem::BufferCollectionTokenMarker>, Error> {
    let (duplicate_token, duplicate_token_server_end) =
        create_endpoints::<fsysmem::BufferCollectionTokenMarker>()?;

    token.duplicate(std::u32::MAX, duplicate_token_server_end)?;
    token.sync().await?;

    Ok(duplicate_token)
}
