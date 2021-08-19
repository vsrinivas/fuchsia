// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities for pseudo file implementations

use {
    fidl_fuchsia_io::{
        OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX,
        OPEN_FLAG_POSIX_EXECUTABLE, OPEN_FLAG_POSIX_WRITABLE, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
};

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
    mut readable: bool,
    mut writable: bool,
    mut executable: bool,
    append_allowed: bool,
) -> Result<u32, zx::Status> {
    // Nodes supporting both W+X rights are not supported.
    debug_assert!(!(writable && executable));

    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE;
        readable = false;
        writable = false;
        executable = false;
    }

    if flags & OPEN_FLAG_DIRECTORY != 0 {
        return Err(zx::Status::NOT_DIR);
    }

    if flags & OPEN_FLAG_NOT_DIRECTORY != 0 {
        flags &= !OPEN_FLAG_NOT_DIRECTORY;
    }

    // For files the OPEN_FLAG_POSIX flags are ignored, as they have meaning only for directories.
    flags &= !(OPEN_FLAG_POSIX | OPEN_FLAG_POSIX_WRITABLE | OPEN_FLAG_POSIX_EXECUTABLE);

    let allowed_flags = OPEN_FLAG_NODE_REFERENCE
        | OPEN_FLAG_DESCRIBE
        | OPEN_FLAG_CREATE
        | OPEN_FLAG_CREATE_IF_ABSENT
        | if readable { OPEN_RIGHT_READABLE } else { 0 }
        | if writable { OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE } else { 0 }
        | if writable && append_allowed { OPEN_FLAG_APPEND } else { 0 }
        | if executable { OPEN_RIGHT_EXECUTABLE } else { 0 };

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
        & !allowed_flags
        // TRUNCATE is only allowed if the file is writable.
        | if flags & OPEN_RIGHT_WRITABLE == 0 {
            OPEN_FLAG_TRUNCATE
        } else {
            0
        };

    if !readable && flags & OPEN_RIGHT_READABLE != 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if !writable && flags & OPEN_RIGHT_WRITABLE != 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if !executable && flags & OPEN_RIGHT_EXECUTABLE != 0 {
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

#[cfg(test)]
mod tests {
    use super::new_connection_validate_flags;
    use crate::test_utils::build_flag_combinations;

    use {
        fidl_fuchsia_io::{
            OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY,
            OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fuchsia_zircon as zx,
    };

    fn io_flags_to_rights(flags: u32) -> (bool, bool, bool) {
        return (
            flags & OPEN_RIGHT_READABLE != 0,
            flags & OPEN_RIGHT_WRITABLE != 0,
            flags & OPEN_RIGHT_EXECUTABLE != 0,
        );
    }

    #[track_caller]
    fn ncvf_ok(
        flags: u32,
        readable: bool,
        writable: bool,
        executable: bool,
        expected_new_flags: u32,
    ) {
        assert!(!(writable && executable), "Cannot specify both writable and executable!");
        let res = new_connection_validate_flags(
            flags, readable, writable, executable, /*append_allowed=*/ false,
        );
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
    fn ncvf_err(
        flags: u32,
        readable: bool,
        writable: bool,
        executable: bool,
        expected_status: zx::Status,
    ) {
        let res = new_connection_validate_flags(
            flags, readable, writable, executable, /*append_allowed=*/ false,
        );
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
        // Should drop access flags but preserve OPEN_FLAG_NODE_REFERENCE and OPEN_FLAG_DESCRIBE.
        let preserved_flags = OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE;
        for open_flags in build_flag_combinations(
            OPEN_FLAG_NODE_REFERENCE,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DESCRIBE,
        ) {
            ncvf_ok(open_flags, true, true, false, open_flags & preserved_flags);
        }

        ncvf_err(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DIRECTORY,
            true,
            true,
            false,
            zx::Status::NOT_DIR,
        );

        ncvf_ok(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_NOT_DIRECTORY,
            true,
            true,
            false,
            OPEN_FLAG_NODE_REFERENCE,
        );
    }

    #[test]
    fn new_connection_validate_flags_posix() {
        // OPEN_FLAG_POSIX is ignored for files.
        for open_flags in build_flag_combinations(
            OPEN_FLAG_POSIX,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
        ) {
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            // Skip disallowed W+X combinations.
            if writable && executable {
                continue;
            }
            ncvf_ok(open_flags, readable, writable, executable, open_flags & !OPEN_FLAG_POSIX);
        }
    }

    #[test]
    fn new_connection_validate_flags_create() {
        for open_flags in build_flag_combinations(
            OPEN_FLAG_CREATE,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_CREATE_IF_ABSENT,
        ) {
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            ncvf_ok(open_flags, readable, writable, executable, open_flags);
        }
    }

    #[test]
    fn new_connection_validate_flags_truncate() {
        ncvf_err(
            OPEN_RIGHT_READABLE | OPEN_FLAG_TRUNCATE,
            true,
            true,
            false,
            zx::Status::INVALID_ARGS,
        );
        ncvf_ok(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
            true,
            true,
            false,
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_TRUNCATE,
        );
        ncvf_err(
            OPEN_RIGHT_READABLE | OPEN_FLAG_TRUNCATE,
            true,
            false,
            false,
            zx::Status::INVALID_ARGS,
        );
    }

    #[test]
    fn new_connection_validate_flags_append() {
        ncvf_err(
            OPEN_RIGHT_READABLE | OPEN_FLAG_APPEND,
            true,
            false,
            false,
            zx::Status::NOT_SUPPORTED,
        );
        ncvf_err(
            OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND,
            true,
            true,
            false,
            zx::Status::INVALID_ARGS,
        );
    }

    #[test]
    fn new_connection_validate_flags_open_rights() {
        for open_flags in build_flag_combinations(
            0,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
        ) {
            let (readable, writable, executable) = io_flags_to_rights(open_flags);

            // Ensure all combinations are valid except when both writable and executable are set,
            // as this combination is disallowed.
            if !(writable && executable) {
                ncvf_ok(open_flags, readable, writable, executable, open_flags);
            }

            // Ensure we report ACCESS_DENIED if open_flags exceeds the supported connection rights.
            if readable && !(writable && executable) {
                ncvf_err(open_flags, false, writable, executable, zx::Status::ACCESS_DENIED);
            }
            if writable {
                ncvf_err(open_flags, readable, false, executable, zx::Status::ACCESS_DENIED);
            }
            if executable {
                ncvf_err(open_flags, readable, writable, false, zx::Status::ACCESS_DENIED);
            }
        }
    }
}
