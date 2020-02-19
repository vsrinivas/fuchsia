// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities for pseudo file implementations

use {
    fidl_fuchsia_io::{
        MODE_PROTECTION_MASK, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_MASK,
        OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    libc::{S_IRUSR, S_IWUSR},
};

/// POSIX emulation layer access attributes for all files created with read_only().
pub const POSIX_READ_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IRUSR;

/// POSIX emulation layer access attributes for all files created with write_only().
pub const POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES: u32 = S_IWUSR;

/// POSIX emulation layer access attributes for all files created with read_write().
pub const POSIX_READ_WRITE_PROTECTION_ATTRIBUTES: u32 =
    POSIX_READ_ONLY_PROTECTION_ATTRIBUTES | POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES;

/// Validate that the requested flags for a new connection are valid.  This function will make sure
/// that `flags` only requests read access when `readable` is true, or only write access when
/// `writable` is true, or either when both are true.  It also does some sanity checking and
/// normalization, matching `flags` with the `mode` value.
///
/// On success, returns the validated flags, with some ambiguities cleaned up.  On failure, it
/// returns a [`Status`] indicating the problem.
///
/// `OPEN_FLAG_NODE_REFERENCE` is preserved and prohibits both read and write access.
///
/// Changing this function can be dangerous!  Flags operations may have security implications.
pub fn new_connection_validate_flags(
    mut flags: u32,
    mode: u32,
    mut readable: bool,
    mut writable: bool,
) -> Result<u32, Status> {
    // There should be no MODE_TYPE_* flags set, except for, possibly, MODE_TYPE_FILE when the
    // target is a pseudo file.
    if (mode & !MODE_PROTECTION_MASK) & !MODE_TYPE_FILE != 0 {
        if mode & MODE_TYPE_MASK == MODE_TYPE_DIRECTORY {
            return Err(Status::NOT_DIR);
        } else {
            return Err(Status::INVALID_ARGS);
        };
    }

    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE;
        readable = false;
        writable = false;
    }

    if flags & OPEN_FLAG_DIRECTORY != 0 {
        return Err(Status::NOT_DIR);
    }

    if flags & OPEN_FLAG_NOT_DIRECTORY != 0 {
        flags &= !OPEN_FLAG_NOT_DIRECTORY;
    }

    // For files OPEN_FLAG_POSIX is just ignored, as it has meaning only for directories.
    flags &= !OPEN_FLAG_POSIX;

    let allowed_flags = OPEN_FLAG_NODE_REFERENCE
        | OPEN_FLAG_DESCRIBE
        | OPEN_FLAG_CREATE
        | OPEN_FLAG_CREATE_IF_ABSENT
        | if readable { OPEN_RIGHT_READABLE } else { 0 }
        | if writable { OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE } else { 0 };

    let prohibited_flags = (0 | if readable {
            OPEN_FLAG_TRUNCATE
        } else {
            0
        } | if writable {
            OPEN_FLAG_APPEND
        } else {
            0
        })
        // allowed_flags takes precedence over prohibited_flags.
        & !allowed_flags;

    if !readable && flags & OPEN_RIGHT_READABLE != 0 {
        return Err(Status::ACCESS_DENIED);
    }

    if !writable && flags & OPEN_RIGHT_WRITABLE != 0 {
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

#[cfg(test)]
mod tests {
    use super::new_connection_validate_flags;

    use {
        fidl_fuchsia_io::{
            MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_SOCKET, OPEN_FLAG_APPEND,
            OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
            OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE,
            OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_zircon::Status,
    };

    macro_rules! ncvf_ok {
        ($flags:expr, $mode:expr, $readable:expr, $writable:expr, $expected_new_flags:expr $(,)*) => {{
            let res = new_connection_validate_flags($flags, $mode, $readable, $writable);
            match res {
                Ok(new_flags) => assert_eq!(
                    $expected_new_flags, new_flags,
                    "new_connection_validate_flags returned unexpected set of flags.\n\
                     Expected: {:X}\n\
                     Actual: {:X}",
                    $expected_new_flags, new_flags
                ),
                Err(status) => panic!("new_connection_validate_flags failed.  Status: {}", status),
            }
        }};
    }

    macro_rules! ncvf_err {
        ($flags:expr, $mode:expr, $readable:expr, $writable:expr, $expected_status:expr $(,)*) => {{
            let res = new_connection_validate_flags($flags, $mode, $readable, $writable);
            match res {
                Ok(new_flags) => panic!(
                    "new_connection_validate_flags should have failed.  \
                     Got new flags: {:X}",
                    new_flags
                ),
                Err(status) => assert_eq!($expected_status, status),
            }
        }};
    }

    #[test]
    fn new_connection_validate_flags_node_reference() {
        // OPEN_FLAG_NODE_REFERENCE is preserved.
        ncvf_ok!(OPEN_FLAG_NODE_REFERENCE, 0, true, true, OPEN_FLAG_NODE_REFERENCE);

        // Access flags are dropped.
        ncvf_ok!(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE,
            0,
            true,
            true,
            OPEN_FLAG_NODE_REFERENCE
        );
        ncvf_ok!(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE,
            0,
            true,
            true,
            OPEN_FLAG_NODE_REFERENCE
        );

        // OPEN_FLAG_DESCRIBE is preserved though.
        ncvf_ok!(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
            0,
            true,
            true,
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
        );
        ncvf_ok!(
            OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
            0,
            true,
            true,
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE
        );

        ncvf_err!(OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DIRECTORY, 0, true, true, Status::NOT_DIR);

        ncvf_ok!(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_NOT_DIRECTORY,
            0,
            true,
            true,
            OPEN_FLAG_NODE_REFERENCE
        );
    }

    #[test]
    fn new_connection_validate_flags_posix() {
        // OPEN_FLAG_POSIX is ignored for files.
        ncvf_ok!(OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX, 0, true, true, OPEN_RIGHT_READABLE);
        ncvf_ok!(OPEN_RIGHT_WRITABLE | OPEN_FLAG_POSIX, 0, true, true, OPEN_RIGHT_WRITABLE);
        ncvf_ok!(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_POSIX,
            0,
            true,
            true,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE
        );
    }

    #[test]
    fn new_connection_validate_flags_create() {
        ncvf_ok!(
            OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE,
            0,
            true,
            false,
            OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE
        );
        ncvf_ok!(
            OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
            0,
            true,
            false,
            OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
        );
        ncvf_ok!(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE,
            0,
            false,
            true,
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE
        );
        ncvf_ok!(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
            0,
            false,
            true,
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT,
        );
    }

    #[test]
    fn new_connection_validate_flags_truncate() {
        ncvf_ok!(
            OPEN_RIGHT_READABLE | OPEN_FLAG_TRUNCATE,
            0,
            true,
            true,
            OPEN_RIGHT_READABLE | OPEN_FLAG_TRUNCATE
        );
        ncvf_ok!(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
            0,
            true,
            true,
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE
        );
        ncvf_err!(OPEN_RIGHT_READABLE | OPEN_FLAG_TRUNCATE, 0, true, false, Status::INVALID_ARGS);
    }

    #[test]
    fn new_connection_validate_flags_append() {
        ncvf_err!(OPEN_RIGHT_READABLE | OPEN_FLAG_APPEND, 0, true, false, Status::NOT_SUPPORTED);
        ncvf_err!(OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND, 0, true, true, Status::INVALID_ARGS);
    }

    #[test]
    fn new_connection_validate_flags_mode_file() {
        ncvf_ok!(OPEN_RIGHT_READABLE, MODE_TYPE_FILE, true, true, OPEN_RIGHT_READABLE);
    }

    #[test]
    fn new_connection_validate_flags_mode_directory() {
        ncvf_err!(OPEN_RIGHT_READABLE, MODE_TYPE_DIRECTORY, true, true, Status::NOT_DIR);
    }

    #[test]
    fn new_connection_validate_flags_mode_socket() {
        ncvf_err!(OPEN_RIGHT_READABLE, MODE_TYPE_SOCKET, true, true, Status::INVALID_ARGS);
    }

    #[test]
    fn new_connection_validate_flags_read_only() {
        ncvf_ok!(OPEN_RIGHT_READABLE, MODE_TYPE_FILE, true, false, OPEN_RIGHT_READABLE);
        ncvf_err!(OPEN_RIGHT_WRITABLE, MODE_TYPE_FILE, true, false, Status::ACCESS_DENIED);
        ncvf_err!(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            true,
            false,
            Status::ACCESS_DENIED
        );
    }

    #[test]
    fn new_connection_validate_flags_write_only() {
        ncvf_ok!(OPEN_RIGHT_WRITABLE, MODE_TYPE_FILE, false, true, OPEN_RIGHT_WRITABLE);
        ncvf_err!(OPEN_RIGHT_READABLE, MODE_TYPE_FILE, false, true, Status::ACCESS_DENIED);
        ncvf_err!(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            false,
            true,
            Status::ACCESS_DENIED
        );
    }

    #[test]
    fn new_connection_validate_flags_read_write() {
        ncvf_ok!(OPEN_RIGHT_READABLE, 0, true, true, OPEN_RIGHT_READABLE);
        ncvf_ok!(OPEN_RIGHT_WRITABLE, 0, true, true, OPEN_RIGHT_WRITABLE);
        ncvf_ok!(
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            true,
            true,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE
        );
    }
}
