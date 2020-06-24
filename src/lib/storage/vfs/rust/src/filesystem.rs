// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
};

pub(crate) mod simple;

/// An implementation of a FilesystemRename represents a filesystem that supports
/// renaming nodes inside it.
pub trait FilesystemRename: Sync + Send {
    fn rename(
        &self,
        src_dir: Arc<Any + Sync + Send + 'static>,
        src_name: String,
        dst_dir: Arc<Any + Sync + Send + 'static>,
        dst_name: String,
    ) -> Result<(), Status>;
}

/// A Filesystem provides operations which might require synchronisation across an entire
/// filesystem. Currently it only provides `rename()`.
pub trait Filesystem: FilesystemRename {}
