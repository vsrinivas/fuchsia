// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by several directory implementations.

use {
    crate::common::stricter_or_same_rights,
    crate::directory::entry::EntryInfo,
    byteorder::{LittleEndian, WriteBytesExt},
    fidl_fuchsia_io::{
        MAX_FILENAME, MODE_PROTECTION_MASK, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE,
        OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    static_assertions::assert_eq_size,
    std::{io::Write, mem::size_of},
};

/// Compares flags provided for a new connection with the flags of a parent connection.  Returns
/// adjusted flags for the new connection (cleaning up some ambiguities) or a fidl Status error, in
/// case new new connection flags are not permitting the connection to be opened.
pub fn new_connection_validate_flags(mut flags: u32, mode: u32) -> Result<u32, Status> {
    // There should be no MODE_TYPE_* flags set, except for, possibly, MODE_TYPE_DIRECTORY when
    // the target is a directory.
    if (mode & !MODE_PROTECTION_MASK) & !MODE_TYPE_DIRECTORY != 0 {
        if (mode & !MODE_PROTECTION_MASK) & MODE_TYPE_FILE != 0 {
            return Err(Status::NOT_FILE);
        } else {
            return Err(Status::INVALID_ARGS);
        };
    }

    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= !OPEN_FLAG_NODE_REFERENCE;
        flags &= OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE;
    }

    // For directories OPEN_FLAG_POSIX means WRITABLE permission.  Parent connection must have
    // already checked the flag, so if it is present it just measn WRITABLE.
    if flags & OPEN_FLAG_POSIX != 0 {
        flags &= !OPEN_FLAG_POSIX;
        flags |= OPEN_RIGHT_WRITABLE;
    }

    let allowed_flags =
        OPEN_FLAG_DESCRIBE | OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

    let prohibited_flags =
        OPEN_FLAG_APPEND | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_FLAG_TRUNCATE;

    // Pseudo directories do not allow mounting at this point.
    if flags & OPEN_RIGHT_ADMIN != 0 {
        return Err(Status::ACCESS_DENIED);
    }

    if flags & prohibited_flags != 0 {
        return Err(Status::INVALID_ARGS);
    }

    if flags & !allowed_flags != 0 {
        return Err(Status::NOT_SUPPORTED);
    }

    Ok(flags)
}

/// Directories need to make sure that connections to child entries do not receive more rights than
/// the conneciton to the directory itself.  Plus there is special handling of the OPEN_FLAG_POSIX
/// flag.  This function should be called before calling [`new_connection_validate_flags`] if both
/// are needed.
pub fn check_child_connection_flags(parent_flags: u32, mut flags: u32) -> Result<u32, Status> {
    if parent_flags & OPEN_RIGHT_WRITABLE == 0 {
        // OPEN_FLAG_POSIX is effectively OPEN_RIGHT_WRITABLE, but with "soft fail", when the
        // target is a directory, so we need to remove it.
        flags &= !OPEN_FLAG_POSIX;
    }

    if stricter_or_same_rights(parent_flags, flags) {
        Ok(flags)
    } else {
        Err(Status::ACCESS_DENIED)
    }
}

/// Splits a `path` string into components, also checking if it is in a canonical form, disallowing
/// any "." and ".." components, as well as empty component names.
pub fn validate_and_split_path(path: &str) -> Result<(impl Iterator<Item = &str>, bool), Status> {
    let is_dir = path.ends_with('/');

    // Disallow empty components, ".", and ".."s.  Path is expected to be canonicalized.  See
    // fxbug.dev/28436 for discussion of empty components.
    {
        let mut check = path.split('/');
        // Allow trailing slash to indicate a directory.
        if is_dir {
            let _ = check.next_back();
        }

        if check.any(|c| c.is_empty() || c == ".." || c == ".") {
            return Err(Status::INVALID_ARGS);
        }
    }

    let mut res = path.split('/');
    if is_dir {
        let _ = res.next_back();
    }
    Ok((res, is_dir))
}

/// A helper to generate binary encodings for the ReadDirents response.  This function will append
/// an entry description as specified by `entry` and `name` to the `buf`, and would return `true`.
/// In case this would cause the buffer size to exceed `max_bytes`, the buffer is then left
/// untouched and a `false` value is returned.
pub fn encode_dirent(buf: &mut Vec<u8>, max_bytes: u64, entry: &EntryInfo, name: &str) -> bool {
    let header_size = size_of::<u64>() + size_of::<u8>() + size_of::<u8>();

    assert_eq_size!(u64, usize);

    if buf.len() + header_size + name.len() > max_bytes as usize {
        return false;
    }

    assert!(
        name.len() < MAX_FILENAME as usize,
        "Entry names are expected to be shorter than MAX_FILENAME ({}) bytes.\n\
         Got entry: '{}'\n\
         Length: {} bytes",
        MAX_FILENAME,
        name,
        name.len()
    );

    assert!(
        MAX_FILENAME <= u8::max_value() as u64,
        "Expecting to be able to store MAX_FILENAME ({}) in one byte.",
        MAX_FILENAME
    );

    buf.write_u64::<LittleEndian>(entry.inode())
        .expect("out should be an in memory buffer that grows as needed");
    buf.write_u8(name.len() as u8).expect("out should be an in memory buffer that grows as needed");
    buf.write_u8(entry.type_()).expect("out should be an in memory buffer that grows as needed");
    buf.write(name.as_ref()).expect("out should be an in memory buffer that grows as needed");

    true
}
