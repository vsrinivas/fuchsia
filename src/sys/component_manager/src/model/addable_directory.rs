// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::NodeMarker,
    fuchsia_vfs_pseudo_fs::directory::{self, entry::DirectoryEntry},
    futures::future::BoxFuture,
};

/// Trait that attempts to add an entry to a pseudo-fs directory, without waiting for a result.
/// This wraps the `add_entry` method of structs that implement DirectoryEntry, streamlining
/// the error type to `ModelError`.
pub trait AddableDirectory<'entries> {
    /// Adds an entry to a directory. Named `add_node` instead of `add_entry` to avoid conflicts.
    fn add_node<Entry>(
        &mut self,
        name: &str,
        entry: Entry,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError>
    where
        Entry: DirectoryEntry + 'entries;
}

/// Trait that attempts to add an entry to a pseudo-fs directory, waiting for a result.
/// This wraps the `add_entry_res` method of structs that implement Controller, streamlining
/// the error type to `ModelError`.
pub trait AddableDirectoryWithResult<'entries> {
    /// Adds an entry to a directory. Named `add_node` instead of `add_entry_res` to avoid conflicts.
    fn add_node<'a, Entry>(
        &'a self,
        name: &'a str,
        entry: Entry,
        moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<(), ModelError>>
    where
        Entry: DirectoryEntry + 'entries;

    // Adds a connection to a directory. Nameed 'open_node' instead of 'open' to avoid conflicts.
    fn open_node<'a>(
        &'a self,
        flags: u32,
        mode: u32,
        path: Vec<String>,
        server_end: ServerEnd<NodeMarker>,
        moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<(), ModelError>>;
}

impl<'entries> AddableDirectory<'entries> for directory::simple::Simple<'entries> {
    fn add_node<Entry>(
        &mut self,
        name: &str,
        entry: Entry,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError>
    where
        Entry: DirectoryEntry + 'entries,
    {
        self.add_entry(name, entry).map_err(|_| ModelError::add_entry_error(moniker.clone(), name))
    }
}

impl<'entries> AddableDirectory<'entries> for directory::controlled::Controlled<'entries> {
    fn add_node<Entry>(
        &mut self,
        name: &str,
        entry: Entry,
        moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError>
    where
        Entry: DirectoryEntry + 'entries,
    {
        self.add_entry(name, entry).map_err(|_| ModelError::add_entry_error(moniker.clone(), name))
    }
}

impl<'entries> AddableDirectoryWithResult<'entries>
    for directory::controlled::Controller<'entries>
{
    fn add_node<'a, Entry>(
        &'a self,
        name: &'a str,
        entry: Entry,
        moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<(), ModelError>>
    where
        Entry: DirectoryEntry + 'entries,
    {
        Box::pin(async move {
            self.add_entry_res(String::from(name), entry).await
                .map_err(|_| ModelError::add_entry_error(moniker.clone(), name))
        })
    }

    fn open_node<'a>(
        &'a self,
        flags: u32,
        mode: u32,
        path: Vec<String>,
        server_end: ServerEnd<NodeMarker>,
        moniker: &'a AbsoluteMoniker,
    ) -> BoxFuture<Result<(), ModelError>> {
        let relative_path = path.join("/");
        Box::pin(async move {
            self.open(flags, mode, path, server_end).await
                .map_err(|_| ModelError::open_directory_error(moniker.clone(), relative_path))
        })
    }
}
