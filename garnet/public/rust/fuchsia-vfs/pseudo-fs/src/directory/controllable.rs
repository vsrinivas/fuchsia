// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::directory::entry::DirectoryEntry, fuchsia_zircon::Status};

/// A trait the represents the add_entry/remove_entry part of a directory interface.  It is used by
/// the [`controlled::Controller`] to populate a directory "remotely".
pub trait Controllable<'entries>: DirectoryEntry {
    /// Adds a child entry to this directory.  The directory will own the child entry item and will
    /// run it as part of the directory own `poll()` invocation.
    ///
    /// In case of any error new entry returned along with the status code.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn add_boxed_entry(
        &mut self,
        name: &str,
        entry: Box<dyn DirectoryEntry + 'entries>,
    ) -> Result<(), (Status, Box<dyn DirectoryEntry + 'entries>)>;

    /// Removes a child entry from this directory.  In case an entry with the matching name was
    /// found, it will be returned to the caller.
    ///
    /// Possible errors are:
    ///   * `name` exceeding [`MAX_FILENAME`] bytes in length.
    ///   * An entry with the same name is already present in the directory.
    fn remove_entry(
        &mut self,
        name: &str,
    ) -> Result<Option<Box<dyn DirectoryEntry + 'entries>>, Status>;
}
