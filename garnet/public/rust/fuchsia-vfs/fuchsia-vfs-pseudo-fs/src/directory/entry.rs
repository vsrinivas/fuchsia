// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common trait for all the directory entry objects.

#![warn(missing_docs)]

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeMarker, DIRENT_TYPE_BLOCK_DEVICE, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
        DIRENT_TYPE_SERVICE, DIRENT_TYPE_SOCKET, DIRENT_TYPE_UNKNOWN,
    },
    futures::{future::FusedFuture, Future},
    std::marker::Unpin,
    void::Void,
};

/// Information about a directory entry, used to populate ReadDirents() output.
/// The first element is the inode number, or INO_UNKNOWN (from io.fidl) if not set, and the second
/// element is one of the DIRENT_TYPE_* constants defined in the io.fidl.
pub struct EntryInfo(u64, u8);

impl EntryInfo {
    /// Constructs a new directory entry information object.
    pub fn new(inode: u64, type_: u8) -> EntryInfo {
        match type_ {
            DIRENT_TYPE_UNKNOWN
            | DIRENT_TYPE_DIRECTORY
            | DIRENT_TYPE_BLOCK_DEVICE
            | DIRENT_TYPE_FILE
            | DIRENT_TYPE_SOCKET
            | DIRENT_TYPE_SERVICE => EntryInfo(inode, type_),
            _ => panic!("Unexpected directory entry type: {}", type_),
        }
    }

    /// Retrives the `inode` argument of the [`EntryInfo::new()`] constructor.
    pub fn inode(&self) -> u64 {
        self.0
    }

    /// Retrives the `type_` argument of the [`EntryInfo::new()`] constructor.
    pub fn type_(&self) -> u8 {
        self.1
    }
}

/// Pseudo directories contain items that implement this trait.  Pseudo directory will own all of
/// its child items and will also run them as part of the pseudo directory own execution.
///
/// [`Future`] is used to "run" this directory entry.  The owner is expected to run
/// [`Future::poll`] continuously, allowing the entry to run for a bit, and the entry will never
/// complete, always returning [`Poll::Pending`].  `Output` of [`Void`] makes sure that it is
/// impossible for the future to complete.
///
/// [`FusedFuture`] is used to distinguish cases when there is no need to run the entry at all,
/// like a case when a directory is empty and has no active connections.
///
/// [`Send`] is necessary to be able to run the whole directory tree in a separate thread.  See
/// #ZX-3593 for details on a `LocalDirectoryEntry` that would allow callbacks that do not need to
/// be `Send`.
pub trait DirectoryEntry: Future<Output = Void> + FusedFuture + Unpin + Send {
    /// Opens a connection to this item if the `path` is empty or a connection to an item inside
    /// this one otherwise.  `path` should not contain any "." or ".." components.  Those are not
    /// processed in any special way.
    ///
    /// `flags` holds one or more of the `OPEN_RIGHT_*`, `OPEN_FLAG_*` constants, while `mode`
    /// holds one of the `MODE_TYPE_*` constants, possibly, along with some bits in the
    /// `MODE_PROTECTION_MASK` section.  Processing of the `flags` value is specific to the item -
    /// in particular, the `OPEN_RIGHT_*` flags need to match the item capabilities.  `MODE_TYPE_*`
    /// flag need to match the item type, or an error should be generated.
    ///
    /// It is a responsibility of the implementation to report any errors through the OnOpen()
    /// even, if flags contain OPEN_FLAG_STATUS, or, in addition, OPEN_FLAG_DESCRIBE.
    ///
    /// In case of an error, the `server_end` object is dropped and the underlying channel will be
    /// closed.
    ///
    /// It is the responsibility of the implementation to send `OnOpen` even on the channel
    /// contained by `server_end` in case `OPEN_FLAG_STATUS` was present in `flags`, and to
    /// populate the `info` part of the event if `OPEN_FLAG_DESCRIBE` was set.  This also applies
    /// to the error cases.
    fn open(
        &mut self,
        flags: u32,
        mode: u32,
        path: &mut Iterator<Item = &str>,
        server_end: ServerEnd<NodeMarker>,
    ) -> Result<(), fidl::Error>;

    /// This method is used to populate ReadDirents() output.
    fn entry_info(&self) -> EntryInfo;
}
