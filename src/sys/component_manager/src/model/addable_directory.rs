// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{error::ModelError, moniker::AbsoluteMoniker},
    fuchsia_vfs_pseudo_fs_mt::directory::{
        entry::DirectoryEntry, entry_container::DirectlyMutable, immutable::simple as pfs,
    },
    std::sync::Arc,
};

type Directory = Arc<pfs::Simple>;

/// Trait that attempts to add an entry to a pseudo-fs-mt directory, without waiting for a result.
/// This wraps the `add_entry` method of structs that implement DirectoryEntry, streamlining
/// the error type to `ModelError`.
pub trait AddableDirectory {
    /// Adds an entry to a directory. Named `add_node` instead of `add_entry` to avoid conflicts.
    fn add_node(
        &mut self,
        name: &str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError>;
}

/// Trait that attempts to add an entry to a pseudo-fs directory, waiting for a result.
/// This wraps the `add_entry_res` method of structs that implement Controller, streamlining
/// the error type to `ModelError`.
pub trait AddableDirectoryWithResult {
    /// Adds an entry to a directory. Named `add_node` instead of `add_entry_res` to avoid conflicts.
    fn add_node<'a>(
        &'a self,
        name: &'a str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &'a AbsoluteMoniker,
    ) -> Result<(), ModelError>;

    fn remove_node<'a>(&'a self, name: &'a str) -> Result<Arc<dyn DirectoryEntry>, ModelError>;
}

impl AddableDirectory for Directory {
    fn add_node(
        &mut self,
        name: &str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        self.clone()
            .add_entry(name, entry)
            .map_err(|_| ModelError::add_entry_error(moniker.clone(), name))
    }
}

impl AddableDirectoryWithResult for Directory {
    fn add_node<'a>(
        &'a self,
        name: &'a str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &'a AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        self.clone()
            .add_entry(String::from(name), entry)
            .map_err(|_| ModelError::add_entry_error(moniker.clone(), name))
    }

    fn remove_node<'a>(&'a self, name: &'a str) -> Result<Arc<dyn DirectoryEntry>, ModelError> {
        self.clone()
            .remove_entry(String::from(name))
            .unwrap()
            .ok_or(ModelError::remove_entry_error(name))
    }
}
