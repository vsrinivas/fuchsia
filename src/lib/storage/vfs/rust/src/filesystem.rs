// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::path::Path,
    async_trait::async_trait,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
};

pub(crate) mod simple;

/// An implementation of a FilesystemRename represents a filesystem that supports
/// renaming nodes inside it.
#[async_trait]
pub trait FilesystemRename: Sync + Send {
    async fn rename(
        &self,
        src_dir: Arc<Any + Sync + Send + 'static>,
        src_name: Path,
        dst_dir: Arc<Any + Sync + Send + 'static>,
        dst_name: Path,
    ) -> Result<(), Status>;
}

/// A Filesystem provides operations which might require synchronisation across an entire
/// filesystem, or which are naturally filesystem-level operations.
pub trait Filesystem: FilesystemRename {
    /// Returns the size of the Filesystem's block device.  This is the granularity at which I/O is
    /// performed.
    fn block_size(&self) -> u32;
}
