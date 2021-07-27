// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by several directory implementations.

use crate::{common::stricter_or_same_rights, directory::entry::EntryInfo};

use {
    byteorder::{LittleEndian, WriteBytesExt},
    fidl_fuchsia_io::{
        MAX_FILENAME, MODE_TYPE_DIRECTORY, MODE_TYPE_MASK, OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX,
        OPEN_FLAG_POSIX_EXECUTABLE, OPEN_FLAG_POSIX_WRITABLE, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    libc::{S_IRUSR, S_IWUSR},
    static_assertions::assert_eq_size,
    std::{io::Write, mem::size_of},
};

/// POSIX emulation layer access attributes for all directories created with empty().
pub const POSIX_DIRECTORY_PROTECTION_ATTRIBUTES: u32 = S_IRUSR | S_IWUSR;

/// Checks flags provided for a new connection.  Returns adjusted flags (cleaning up some
/// ambiguities) or a fidl Status error, in case new new connection flags are not permitting the
/// connection to be opened.
///
/// OPEN_FLAG_NODE_REFERENCE is preserved.
///
/// Changing this function can be dangerous!  Flags operations may have security implications.
pub fn new_connection_validate_flags(mut flags: u32) -> Result<u32, zx::Status> {
    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE;
    }

    if flags & OPEN_FLAG_DIRECTORY != 0 {
        flags &= !OPEN_FLAG_DIRECTORY;
    }

    if flags & OPEN_FLAG_NOT_DIRECTORY != 0 {
        return Err(zx::Status::NOT_FILE);
    }

    // TODO(fxb/37534): Add support for execute rights.
    flags &= !OPEN_FLAG_POSIX_EXECUTABLE;

    // For directories OPEN_FLAG_POSIX means WRITABLE permission.  Parent connection must have
    // already checked the flag, so if it is present it just means WRITABLE.
    if flags & (OPEN_FLAG_POSIX | OPEN_FLAG_POSIX_WRITABLE) != 0 {
        flags &= !(OPEN_FLAG_POSIX | OPEN_FLAG_POSIX_WRITABLE);
        flags |= OPEN_RIGHT_WRITABLE;
    }

    let allowed_flags = OPEN_FLAG_NODE_REFERENCE
        | OPEN_FLAG_DESCRIBE
        | OPEN_FLAG_CREATE
        | OPEN_FLAG_CREATE_IF_ABSENT
        | OPEN_FLAG_DIRECTORY
        | OPEN_RIGHT_READABLE
        | OPEN_RIGHT_WRITABLE;

    let prohibited_flags = OPEN_FLAG_APPEND | OPEN_FLAG_TRUNCATE;

    if flags & OPEN_RIGHT_ADMIN != 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if flags & prohibited_flags != 0 {
        return Err(zx::Status::INVALID_ARGS);
    }

    if flags & !allowed_flags != 0 {
        return Err(zx::Status::NOT_SUPPORTED);
    }

    Ok(flags)
}

/// Directories need to make sure that connections to child entries do not receive more rights than
/// the conneciton to the directory itself.  Plus there is special handling of the OPEN_FLAG_POSIX
/// flag.  This function should be called before calling [`new_connection_validate_flags`] if both
/// are needed.
pub fn check_child_connection_flags(
    parent_flags: u32,
    mut flags: u32,
    mut mode: u32,
) -> Result<(u32, u32), zx::Status> {
    let mode_type = mode & MODE_TYPE_MASK;

    if mode_type == 0 {
        if flags & OPEN_FLAG_DIRECTORY != 0 {
            mode |= MODE_TYPE_DIRECTORY;
        }
    } else {
        if flags & OPEN_FLAG_DIRECTORY != 0 && mode_type != MODE_TYPE_DIRECTORY {
            return Err(zx::Status::INVALID_ARGS);
        }

        if flags & OPEN_FLAG_NOT_DIRECTORY != 0 && mode_type == MODE_TYPE_DIRECTORY {
            return Err(zx::Status::INVALID_ARGS);
        }
    }

    if flags & (OPEN_FLAG_NOT_DIRECTORY | OPEN_FLAG_DIRECTORY)
        == OPEN_FLAG_NOT_DIRECTORY | OPEN_FLAG_DIRECTORY
    {
        return Err(zx::Status::INVALID_ARGS);
    }

    // TODO(fxb/37534): Add support for execute rights.
    flags &= !OPEN_FLAG_POSIX_EXECUTABLE;

    if parent_flags & OPEN_RIGHT_WRITABLE == 0 {
        // OPEN_FLAG_POSIX/OPEN_FLAG_POSIX_WRITABLE is effectively OPEN_RIGHT_WRITABLE, but with
        // "soft fail", when the target is a directory, so we need to remove it.
        flags &= !(OPEN_FLAG_POSIX | OPEN_FLAG_POSIX_WRITABLE);
    }

    if flags & OPEN_FLAG_CREATE != 0 && parent_flags & OPEN_RIGHT_WRITABLE == 0 {
        // Can only use CREATE flags if the parent connection is writable.
        return Err(zx::Status::ACCESS_DENIED);
    }

    if stricter_or_same_rights(parent_flags, flags) {
        Ok((flags, mode))
    } else {
        Err(zx::Status::ACCESS_DENIED)
    }
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
        name.len() <= MAX_FILENAME as usize,
        "Entry names are expected to be no longer than MAX_FILENAME ({}) bytes.\n\
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

#[cfg(test)]
mod tests {
    use super::{check_child_connection_flags, new_connection_validate_flags};
    use crate::test_utils::build_flag_combinations;

    use {
        fidl_fuchsia_io::{
            OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY,
            OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fuchsia_zircon as zx,
    };

    #[track_caller]
    fn ncvf_ok(flags: u32, expected_new_flags: u32) {
        let res = new_connection_validate_flags(flags);
        match res {
            Ok(new_flags) => assert_eq!(
                expected_new_flags, new_flags,
                "new_connection_validate_flags returned unexpected set of flags.\n\
                    Expected: {:X}\n\
                    Actual: {:X}",
                expected_new_flags, new_flags
            ),
            Err(status) => panic!("new_connection_validate_flags failed.  Status: {}", status),
        }
    }

    #[track_caller]
    fn ncvf_err(flags: u32, expected_status: zx::Status) {
        let res = new_connection_validate_flags(flags);
        match res {
            Ok(new_flags) => panic!(
                "new_connection_validate_flags should have failed.  \
                    Got new flags: {:X}",
                new_flags
            ),
            Err(status) => assert_eq!(expected_status, status),
        }
    }

    #[test]
    fn new_connection_validate_flags_node_reference() {
        // OPEN_FLAG_NODE_REFERENCE and OPEN_FLAG_DESCRIBE should be preserved.
        const PRESERVED_FLAGS: u32 = OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE;
        for open_flags in build_flag_combinations(
            OPEN_FLAG_NODE_REFERENCE,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE | OPEN_FLAG_DIRECTORY,
        ) {
            ncvf_ok(open_flags, open_flags & PRESERVED_FLAGS);
        }

        ncvf_err(OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_NOT_DIRECTORY, zx::Status::NOT_FILE);
    }

    #[test]
    fn new_connection_validate_flags_posix() {
        for open_flags in
            build_flag_combinations(OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
        {
            ncvf_ok(open_flags, OPEN_RIGHT_WRITABLE | (open_flags & !OPEN_FLAG_POSIX));
        }
    }

    #[test]
    fn new_connection_validate_flags_admin() {
        // Currently not supported.
        ncvf_err(OPEN_RIGHT_ADMIN, zx::Status::ACCESS_DENIED);
    }

    #[test]
    fn new_connection_validate_flags_create() {
        for open_flags in build_flag_combinations(
            OPEN_FLAG_CREATE,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        ) {
            ncvf_ok(open_flags, open_flags);
        }
    }

    #[test]
    fn new_connection_validate_flags_append() {
        ncvf_err(OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND, zx::Status::INVALID_ARGS);
    }

    #[test]
    fn new_connection_validate_flags_truncate() {
        ncvf_err(OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE, zx::Status::INVALID_ARGS);
    }

    #[test]
    fn check_child_connection_flags_create_flags() {
        assert!(check_child_connection_flags(OPEN_RIGHT_WRITABLE, OPEN_FLAG_CREATE, 0).is_ok());
        assert!(check_child_connection_flags(
            OPEN_RIGHT_WRITABLE,
            OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
            0
        )
        .is_ok());

        assert_eq!(
            check_child_connection_flags(0, OPEN_FLAG_CREATE, 0),
            Err(zx::Status::ACCESS_DENIED),
        );
        assert_eq!(
            check_child_connection_flags(0, OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT, 0),
            Err(zx::Status::ACCESS_DENIED),
        );
    }
}
