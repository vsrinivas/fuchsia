// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::error::ModelError,
    cm_moniker::InstancedAbsoluteMoniker,
    std::sync::Arc,
    vfs::directory::{entry::DirectoryEntry, helper::DirectlyMutable, immutable::simple as pfs},
};

type Directory = Arc<pfs::Simple>;

/// Trait that attempts to add an entry to a vfs directory, without waiting for a result.
/// This wraps the `add_entry` method of structs that implement DirectoryEntry, streamlining
/// the error type to `ModelError`.
pub trait AddableDirectory {
    /// Adds an entry to a directory. Named `add_node` instead of `add_entry` to avoid conflicts.
    fn add_node(
        &mut self,
        name: &str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &InstancedAbsoluteMoniker,
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
        moniker: &'a InstancedAbsoluteMoniker,
    ) -> Result<(), ModelError>;

    /// Removes an entry in the directory.
    ///
    /// Returns the entry to the caller if the entry was present in the directory.
    fn remove_node<'a>(
        &'a self,
        name: &'a str,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, ModelError>;
}

impl AddableDirectory for Directory {
    fn add_node(
        &mut self,
        name: &str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &InstancedAbsoluteMoniker,
    ) -> Result<(), ModelError> {
        self.clone()
            .add_entry(name, entry)
            .map_err(|_| ModelError::add_entry_error(moniker.to_absolute_moniker(), name))
    }
}

impl AddableDirectoryWithResult for Directory {
    fn add_node<'a>(
        &'a self,
        name: &'a str,
        entry: Arc<dyn DirectoryEntry>,
        moniker: &'a InstancedAbsoluteMoniker,
    ) -> Result<(), ModelError> {
        self.clone()
            .add_entry(String::from(name), entry)
            .map_err(|_| ModelError::add_entry_error(moniker.to_absolute_moniker(), name))
    }

    fn remove_node<'a>(
        &'a self,
        name: &'a str,
    ) -> Result<Option<Arc<dyn DirectoryEntry>>, ModelError> {
        self.clone()
            .remove_entry(String::from(name), false)
            .map_err(|_| ModelError::remove_entry_error(name))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, moniker::AbsoluteMonikerBase,
        std::convert::TryInto, vfs::file::vmo::read_only_static,
    };

    #[test]
    fn addable_with_result_add_ok() {
        let dir = vfs::directory::immutable::simple();

        assert!(
            dir.add_node(
                "node_name",
                read_only_static(b"test"),
                &InstancedAbsoluteMoniker::parse_str("/node:0").unwrap(),
            )
            .is_ok(),
            "add node with valid name should succeed"
        );
    }

    #[test]
    fn addable_with_result_add_error() {
        let dir = vfs::directory::immutable::simple();

        let err = dir
            .add_node(
                "node_name/with/separators",
                read_only_static(b"test"),
                &InstancedAbsoluteMoniker::parse_str("/node:0").unwrap(),
            )
            .expect_err("add entry with path separator should fail");
        assert_matches!(err, ModelError::AddEntryError { .. });
    }

    #[test]
    fn addable_with_result_remove_ok() {
        let dir = vfs::directory::immutable::simple();

        dir.add_node(
            "node_name",
            read_only_static(b"test"),
            &InstancedAbsoluteMoniker::parse_str("/node:0").unwrap(),
        )
        .expect("add node with valid name should succeed");

        let entry = dir.remove_node("node_name").expect("remove node should succeed");
        assert!(entry.is_some(), "entry should have existed before remove");
    }

    #[test]
    fn addable_with_result_remove_missing_ok() {
        let dir = vfs::directory::immutable::simple();

        let entry = dir.remove_node("does_not_exist").expect("remove node should succeed");
        assert!(entry.is_none(), "entry should not have existed before remove");
    }

    #[test]
    fn addable_with_result_remove_error() {
        let dir = vfs::directory::immutable::simple();

        let entry_name = "x".repeat((vfs::MAX_NAME_LENGTH + 1).try_into().unwrap());
        assert!(dir.remove_node(&entry_name).is_err(), "remove node should fail");
    }
}
