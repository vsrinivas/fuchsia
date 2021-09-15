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
        OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, VMO_FLAG_EXACT,
        VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ, VMO_FLAG_WRITE,
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

/// Converts the set of validated VMO flags to their respective zx::Rights.
pub fn vmo_flags_to_rights(vmo_flags: u32) -> zx::Rights {
    // Map VMO flags to their respective rights.
    let mut rights = zx::Rights::NONE;
    if vmo_flags & VMO_FLAG_READ != 0 {
        rights |= zx::Rights::READ;
    }
    if vmo_flags & VMO_FLAG_WRITE != 0 {
        rights |= zx::Rights::WRITE;
    }
    if vmo_flags & VMO_FLAG_EXEC != 0 {
        rights |= zx::Rights::EXECUTE;
    }

    rights
}

/// Validate flags passed to `get_buffer` against the underlying connection flags.
/// Returns Ok() if the flags were validated, and an Error(zx::Status) otherwise.
///
/// Changing this function can be dangerous!  Flags operations may have security implications.
pub fn get_buffer_validate_flags(vmo_flags: u32, connection_flags: u32) -> Result<(), zx::Status> {
    // Disallow inconsistent flag combination.
    if vmo_flags & VMO_FLAG_PRIVATE != 0 && vmo_flags & VMO_FLAG_EXACT != 0 {
        return Err(zx::Status::INVALID_ARGS);
    }

    // Ensure the requested rights in vmo_flags do not exceed those of the underlying connection.
    if vmo_flags & VMO_FLAG_READ != 0 && connection_flags & OPEN_RIGHT_READABLE == 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }
    if vmo_flags & VMO_FLAG_WRITE != 0 && connection_flags & OPEN_RIGHT_WRITABLE == 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }
    if vmo_flags & VMO_FLAG_EXEC != 0 && connection_flags & OPEN_RIGHT_EXECUTABLE == 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }

    // As documented in the io.fidl protocol, if VMO_FLAG_EXEC is requested, ensure that the
    // connection also has OPEN_RIGHT_READABLE.
    if vmo_flags & VMO_FLAG_EXEC != 0 && connection_flags & OPEN_RIGHT_READABLE == 0 {
        return Err(zx::Status::ACCESS_DENIED);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{get_buffer_validate_flags, new_connection_validate_flags, vmo_flags_to_rights};
    use crate::test_utils::build_flag_combinations;

    use {
        fidl_fuchsia_io::{
            OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY,
            OPEN_FLAG_POSIX, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE, VMO_FLAG_EXACT, VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ,
            VMO_FLAG_WRITE,
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

    fn rights_to_vmo_flags(readable: bool, writable: bool, executable: bool) -> u32 {
        return if readable { VMO_FLAG_READ } else { 0 }
            | if writable { VMO_FLAG_WRITE } else { 0 }
            | if executable { VMO_FLAG_EXEC } else { 0 };
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

    /// Validates that the passed VMO flags are correctly mapped to their respective Rights.
    #[test]
    fn test_vmo_flags_to_rights() {
        for vmo_flags in build_flag_combinations(0, VMO_FLAG_READ | VMO_FLAG_WRITE | VMO_FLAG_EXEC)
        {
            let rights: zx::Rights = vmo_flags_to_rights(vmo_flags);
            assert_eq!(vmo_flags & VMO_FLAG_READ != 0, rights.contains(zx::Rights::READ));
            assert_eq!(vmo_flags & VMO_FLAG_WRITE != 0, rights.contains(zx::Rights::WRITE));
            assert_eq!(vmo_flags & VMO_FLAG_EXEC != 0, rights.contains(zx::Rights::EXECUTE));
        }
    }

    #[test]
    fn get_buffer_validate_flags_invalid() {
        // Cannot specify both PRIVATE and EXACT at the same time, since they conflict.
        assert_eq!(
            get_buffer_validate_flags(VMO_FLAG_PRIVATE | VMO_FLAG_EXACT, 0),
            Err(zx::Status::INVALID_ARGS)
        );
    }

    /// Ensure that the check passes if we request the same or less rights than the connection has.
    #[test]
    fn get_buffer_validate_flags_less_rights() {
        for open_flags in build_flag_combinations(
            0,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
        ) {
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            let vmo_flags = rights_to_vmo_flags(readable, writable, executable);

            // The io1.fidl protocol specifies that VMO_FLAG_EXEC requires the connection to be both
            // readable and executable.
            if executable && !readable {
                assert_eq!(
                    get_buffer_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
                continue;
            }

            // Ensure that we can open the VMO with the same rights as the connection.
            get_buffer_validate_flags(vmo_flags, open_flags).expect("Failed to validate flags");

            // Ensure that we can also open the VMO with *less* rights than the connection has.
            if readable {
                let vmo_flags = rights_to_vmo_flags(false, writable, false);
                get_buffer_validate_flags(vmo_flags, open_flags).expect("Failed to validate flags");
            }
            if writable {
                let vmo_flags = rights_to_vmo_flags(readable, false, executable);
                get_buffer_validate_flags(vmo_flags, open_flags).expect("Failed to validate flags");
            }
            if executable {
                let vmo_flags = rights_to_vmo_flags(true, writable, false);
                get_buffer_validate_flags(vmo_flags, open_flags).expect("Failed to validate flags");
            }
        }
    }

    /// Ensure that vmo_flags cannot exceed rights of connection_flags.
    #[test]
    fn get_buffer_validate_flags_more_rights() {
        for open_flags in build_flag_combinations(
            0,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_EXECUTABLE,
        ) {
            // Ensure we cannot return a VMO with more rights than the connection itself has.
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            if !readable {
                let vmo_flags = rights_to_vmo_flags(true, writable, executable);
                assert_eq!(
                    get_buffer_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
            if !writable {
                let vmo_flags = rights_to_vmo_flags(readable, true, false);
                assert_eq!(
                    get_buffer_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
            if !executable {
                let vmo_flags = rights_to_vmo_flags(readable, false, true);
                assert_eq!(
                    get_buffer_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
        }
    }
}
