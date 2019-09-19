// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Mutable directories need to create new entries inside themselves.  [`EntryConstructor`] is a
//! mechanism that allows the library user to control this process.

use crate::{directory::entry::DirectoryEntry, path::Path};

use {
    fidl_fuchsia_io::{
        MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_MASK, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NOT_DIRECTORY,
    },
    fuchsia_zircon::Status,
    std::sync::Arc,
};

/// Defines the type of the new entry to be created via the [`EntryConstructor::create_entry()`]
/// call.
///
/// It is set by certain flags in the `flags` argument of the `Directory::Open()` io.fidl call, as
/// well as the `TYPE` part of the `mode` argument.  While it is possible to issue an `Open` call
/// that will try to create, say, a "service" or a "block device", these use cases are undefined at
/// the moment.  So the library hides them from the library users and will just return an error to
/// the FIDL client.  Should we have a use case where it would be possible to create a service or
/// another kind of entry we should augment this enumeration will a corresponding type.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum NewEntryType {
    Directory,
    File,
}

/// Should a file system support entry creation, it will need to provide an object implementing
/// this trait and register it in the [`ExecutionScope`].  Connections will use this trait when
/// constructing new entries.
pub trait EntryConstructor {
    /// This method is called when a new entry need to be constructed.  The constructor is only
    /// responsibility is to create a new entry.  It does not need to attach the entry to the
    /// parent, nor does it need to establish connections to the new entry.  All of that is
    /// handled elsewhere.  But `parent` can be used to understand a position in the tree, if
    /// different kinds of entries need to be constructed in different parts of the file system
    /// tree.
    ///
    /// `parent` refers to the parent directory, where a new entry need to be added.
    ///
    /// `what` is the kind of entry that the user has requested.
    ///
    /// `name` is the name of the new entry relative to the `parent`.
    ///
    /// If a missing entry was encountered before reaching a leaf node, `path` will be a non-empty
    /// path that goes "inside" of the missing entry.  Common behaviour is that when this is
    /// non-empty a `NOT_FOUND` error is returned, as it is generally the case that only a leaf
    /// entry can be created.  But if a file system wants to allow creation of more than one
    /// element of a path, this argument allows for that.
    fn create_entry(
        self: Arc<Self>,
        parent: Arc<dyn DirectoryEntry>,
        what: NewEntryType,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status>;
}

impl NewEntryType {
    /// Given a `flags` and a `mode` arguments for a `Directory::Open()` io.fidl call this method
    /// will select proper type from the [`NewEntryType`] enum or will return an error to be sent
    /// to the
    /// client.
    ///
    /// Sometimes the caller knows that the expected type of the entry should be a directory.  For
    /// example, when the path of the entry ends with a '/'.  In this case `force_directory` will
    /// make sure that `flags` and `mode` combination allows or specifies a directory, returning an
    /// error when it is not the case.
    pub fn from_flags_and_mode(
        flags: u32,
        mut mode: u32,
        force_directory: bool,
    ) -> Result<NewEntryType, Status> {
        mode = mode & MODE_TYPE_MASK;

        // Allow only MODE_TYPE_DIRECTORY or MODE_TYPE_FILE for `mode`, if mode is set.
        if mode != MODE_TYPE_DIRECTORY && mode != MODE_TYPE_FILE && mode != 0 {
            return Err(Status::NOT_SUPPORTED);
        }

        // Same for `flags`, allow only one of OPEN_FLAG_DIRECTORY or OPEN_FLAG_NOT_DIRECTORY.
        if flags & OPEN_FLAG_DIRECTORY != 0 && flags & OPEN_FLAG_NOT_DIRECTORY != 0 {
            return Err(Status::INVALID_ARGS);
        }

        // If specified, `flags` and `mode` should agree on what they are asking for.
        if (flags & OPEN_FLAG_DIRECTORY != 0 && mode == MODE_TYPE_FILE)
            || (flags & OPEN_FLAG_NOT_DIRECTORY != 0 && mode == MODE_TYPE_DIRECTORY)
        {
            return Err(Status::INVALID_ARGS);
        }

        let type_ = if flags & OPEN_FLAG_DIRECTORY != 0 || mode == MODE_TYPE_DIRECTORY {
            NewEntryType::Directory
        } else if flags & OPEN_FLAG_NOT_DIRECTORY != 0 || mode == MODE_TYPE_FILE {
            NewEntryType::File
        } else {
            // Neither is set, so default to file, unless `force_directory` would make use fail.
            if force_directory {
                NewEntryType::Directory
            } else {
                NewEntryType::File
            }
        };

        if force_directory && type_ == NewEntryType::File {
            Err(Status::INVALID_ARGS)
        } else {
            Ok(type_)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::NewEntryType;

    use {
        fidl_fuchsia_io::{
            MODE_TYPE_BLOCK_DEVICE, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_SERVICE,
            MODE_TYPE_SOCKET, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NOT_DIRECTORY,
        },
        fuchsia_zircon::Status,
    };

    macro_rules! assert_success {
        (flags: $flags:expr,
         mode: $mode:expr,
         force_directory: $force_directory:expr,
         expected: $expected:ident) => {
            match NewEntryType::from_flags_and_mode($flags, $mode, $force_directory) {
                Ok(type_) => assert!(
                    type_ == NewEntryType::$expected,
                    "`from_flags_and_mode` selected wrong entry type.\n\
                     Actual:   {:?}\n\
                     Expected: {:?}",
                    type_,
                    NewEntryType::$expected,
                ),
                Err(status) => panic!("`from_flags_and_mode` failed: {}", status),
            }
        };
    }

    macro_rules! assert_failure {
        (flags: $flags:expr,
         mode: $mode:expr,
         force_directory: $force_directory:expr,
         expected: $expected:expr) => {
            match NewEntryType::from_flags_and_mode($flags, $mode, $force_directory) {
                Ok(type_) => {
                    panic!("`from_flags_and_mode` did not fail.  Got entry type: {:?}", type_)
                }
                Err(status) => assert!(
                    status == $expected,
                    "`from_flags_and_mode` failed as expected but with an unexpected status.\n\
                     Actual:   {}\n\
                     Expected: {}",
                    status,
                    $expected,
                ),
            }
        };
    }

    #[test]
    fn directory() {
        assert_success!(flags: OPEN_FLAG_DIRECTORY,
            mode: 0,
            force_directory: false,
            expected: Directory);
        assert_success!(flags: 0,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: false,
            expected: Directory);
        assert_success!(flags: OPEN_FLAG_DIRECTORY,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: false,
            expected: Directory);
        assert_success!(flags: 0,
            mode: 0,
            force_directory: true,
            expected: Directory);
        assert_success!(flags: OPEN_FLAG_DIRECTORY,
            mode: 0,
            force_directory: true,
            expected: Directory);
        assert_success!(flags: 0,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: true,
            expected: Directory);
        assert_success!(flags: OPEN_FLAG_DIRECTORY,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: true,
            expected: Directory);
    }

    #[test]
    fn file() {
        assert_success!(flags: 0,
            mode: 0,
            force_directory: false,
            expected: File);
        assert_success!(flags: OPEN_FLAG_NOT_DIRECTORY,
            mode: 0,
            force_directory: false,
            expected: File);
        assert_success!(flags: 0,
            mode: MODE_TYPE_FILE,
            force_directory: false,
            expected: File);
        assert_success!(flags: OPEN_FLAG_NOT_DIRECTORY,
            mode: MODE_TYPE_FILE,
            force_directory: false,
            expected: File);
    }

    #[test]
    fn unsupported_types() {
        let status = Status::NOT_SUPPORTED;

        assert_failure!(flags: 0,
            mode: MODE_TYPE_BLOCK_DEVICE,
            force_directory: false,
            expected: status);
        assert_failure!(flags: 0,
            mode: MODE_TYPE_SOCKET,
            force_directory: false,
            expected: status);
        assert_failure!(flags: 0,
            mode: MODE_TYPE_SERVICE,
            force_directory: false,
            expected: status);
    }

    #[test]
    fn invalid_combinations() {
        let status = Status::INVALID_ARGS;

        assert_failure!(flags: OPEN_FLAG_DIRECTORY,
            mode: MODE_TYPE_FILE,
            force_directory: false,
            expected: status);
        assert_failure!(flags: OPEN_FLAG_DIRECTORY,
            mode: MODE_TYPE_FILE,
            force_directory: true,
            expected: status);
        assert_failure!(flags: OPEN_FLAG_NOT_DIRECTORY,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: false,
            expected: status);
        assert_failure!(flags: OPEN_FLAG_NOT_DIRECTORY,
            mode: MODE_TYPE_DIRECTORY,
            force_directory: true,
            expected: status);
    }
}
