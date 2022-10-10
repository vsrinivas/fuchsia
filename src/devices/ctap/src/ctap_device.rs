// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, async_trait::async_trait, fidl_fuchsia_io as fio, std::path::PathBuf};

/// Common trait implemented by the different transport mechanisms for CTAP devices.
/// Note: the ?Send is necessary to allow implementations to make FIDL calls because the auto
/// generated FIDL bindings don't require threadsafe inputs.
#[async_trait(?Send)]
pub trait CtapDevice: Sized {
    /// Returns a new CtapDevice on the proxy at `entry_path` if it exists.
    async fn device(dir_proxy: &fio::DirectoryProxy, entry_path: &PathBuf) -> Result<Self, Error>;
}
