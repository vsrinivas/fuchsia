// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code shared between several modules of the service implementation.

use {
    fidl_fuchsia_io::{
        MODE_PROTECTION_MASK, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_MASK,
        MODE_TYPE_SERVICE, OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    libc::{S_IRUSR, S_IWUSR},
};

/// POSIX emulation layer access attributes for all services created with service().
pub const POSIX_READ_WRITE_PROTECTION_ATTRIBUTES: u32 = S_IRUSR | S_IWUSR;

/// Validate that the requested flags for a new connection are valid.  It is a bit tricky as
/// depending on the presence of the `OPEN_FLAG_NODE_REFERENCE` flag we are effectively validating
/// two different cases: with `OPEN_FLAG_NODE_REFERENCE` the connection will be attached to the
/// service node itself, and without `OPEN_FLAG_NODE_REFERENCE` the connection will be forwarded to
/// the backing service.
///
/// `new_connection_validate_flags` will preserve `OPEN_FLAG_NODE_REFERENCE` to make it easier for
/// the caller to distinguish these two cases.
///
/// On success, returns the validated and cleaned flags.  On failure, it returns a [`Status`]
/// indicating the problem.
///
/// Changing this function can be dangerous!  Flags operations may have security implications.
pub fn new_connection_validate_flags(mut flags: u32, mode: u32) -> Result<u32, Status> {
    // There should be no MODE_TYPE_* flags set, except for, possibly, MODE_TYPE_SOCKET when the
    // target is a service.
    if (mode & !MODE_PROTECTION_MASK) & !MODE_TYPE_SERVICE != 0 {
        if mode & MODE_TYPE_MASK == MODE_TYPE_DIRECTORY {
            return Err(Status::NOT_DIR);
        } else if mode & MODE_TYPE_MASK == MODE_TYPE_FILE {
            return Err(Status::NOT_FILE);
        } else {
            return Err(Status::INVALID_ARGS);
        };
    }

    // A service is not a directory.
    flags &= !OPEN_FLAG_NOT_DIRECTORY;

    // For services OPEN_FLAG_POSIX is just ignored, as it has meaning only for directories.
    flags &= !OPEN_FLAG_POSIX;

    if flags & OPEN_FLAG_DIRECTORY != 0 {
        return Err(Status::NOT_DIR);
    }

    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= !(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        if flags & !OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE != 0 {
            return Err(Status::INVALID_ARGS);
        }
        flags &= OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE;
        return Ok(flags);
    }

    // All the flags we have already checked above and removed.
    debug_assert!(
        flags
            & (OPEN_FLAG_DIRECTORY
                | OPEN_FLAG_NOT_DIRECTORY
                | OPEN_FLAG_POSIX
                | OPEN_FLAG_NODE_REFERENCE)
            == 0
    );

    // A service might only be connected to when both read and write permissions are present.
    if flags & OPEN_RIGHT_READABLE == 0 || flags & OPEN_RIGHT_WRITABLE == 0 {
        return Err(Status::ACCESS_DENIED);
    }
    let allowed_flags = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

    // OPEN_FLAG_DESCRIBE is not allowed when connecting directly to the service itself.
    if flags & OPEN_FLAG_DESCRIBE != 0 {
        return Err(Status::INVALID_ARGS);
    }

    // Anything else is also not allowed.
    if flags & !allowed_flags != 0 {
        return Err(Status::INVALID_ARGS);
    }

    Ok(flags)
}

#[cfg(test)]
mod tests {
    use super::new_connection_validate_flags;

    use {
        fidl_fuchsia_io::{
            MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, MODE_TYPE_SERVICE, MODE_TYPE_SOCKET,
            OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE,
            OPEN_FLAG_NOT_DIRECTORY, OPEN_FLAG_POSIX, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_zircon::Status,
    };

    /// Assertion for when `new_connection_validate_flags` should succeed => `ncvf_ok`.
    fn ncvf_ok(flags: u32, mode: u32, expected_new_flags: u32) {
        match new_connection_validate_flags(flags, mode) {
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

    /// Assertion for when `new_connection_validate_flags` should fail => `ncvf_err`.
    fn ncvf_err(flags: u32, mode: u32, expected_status: Status) {
        match new_connection_validate_flags(flags, mode) {
            Ok(new_flags) => panic!(
                "new_connection_validate_flags should have failed.\n\
                 Got new flags: {:X}",
                new_flags
            ),
            Err(status) => assert_eq!(expected_status, status),
        }
    }

    /// Common combination for the service tests.
    const READ_WRITE: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE;

    #[test]
    fn node_reference_basic() {
        // OPEN_FLAG_NODE_REFERENCE is preserved.
        ncvf_ok(OPEN_FLAG_NODE_REFERENCE, 0, OPEN_FLAG_NODE_REFERENCE);

        // Access flags are dropped.
        ncvf_ok(OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_READABLE, 0, OPEN_FLAG_NODE_REFERENCE);
        ncvf_ok(OPEN_FLAG_NODE_REFERENCE | OPEN_RIGHT_WRITABLE, 0, OPEN_FLAG_NODE_REFERENCE);

        // OPEN_FLAG_DESCRIBE is preserved.
        ncvf_ok(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
            0,
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
        );
        ncvf_ok(
            OPEN_FLAG_NODE_REFERENCE | READ_WRITE | OPEN_FLAG_DESCRIBE,
            0,
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
        );

        ncvf_ok(OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_NOT_DIRECTORY, 0, OPEN_FLAG_NODE_REFERENCE);
        ncvf_err(OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DIRECTORY, 0, Status::NOT_DIR);
    }

    #[test]
    fn service_basic() {
        // Access flags are required and preserved.
        ncvf_ok(READ_WRITE, 0, READ_WRITE);
        ncvf_err(OPEN_RIGHT_READABLE, 0, Status::ACCESS_DENIED);
        ncvf_err(OPEN_RIGHT_WRITABLE, 0, Status::ACCESS_DENIED);

        // OPEN_FLAG_DESCRIBE is not allowed.
        ncvf_err(READ_WRITE | OPEN_FLAG_DESCRIBE, 0, Status::INVALID_ARGS);

        ncvf_ok(READ_WRITE | OPEN_FLAG_NOT_DIRECTORY, 0, READ_WRITE);
        ncvf_err(READ_WRITE | OPEN_FLAG_DIRECTORY, 0, Status::NOT_DIR);
    }

    #[test]
    fn node_reference_posix() {
        // OPEN_FLAG_POSIX is ignored for services.
        ncvf_ok(OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_POSIX, 0, OPEN_FLAG_NODE_REFERENCE);
        ncvf_ok(
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE | OPEN_FLAG_POSIX,
            0,
            OPEN_FLAG_NODE_REFERENCE | OPEN_FLAG_DESCRIBE,
        );
        ncvf_ok(
            OPEN_FLAG_NODE_REFERENCE | READ_WRITE | OPEN_FLAG_POSIX,
            0,
            OPEN_FLAG_NODE_REFERENCE,
        );
    }

    #[test]
    fn service_posix() {
        // OPEN_FLAG_POSIX is ignored for services.
        ncvf_ok(READ_WRITE | OPEN_FLAG_POSIX, 0, READ_WRITE);
        ncvf_err(READ_WRITE | OPEN_FLAG_DESCRIBE | OPEN_FLAG_POSIX, 0, Status::INVALID_ARGS);
    }

    #[test]
    fn file() {
        ncvf_err(OPEN_FLAG_NODE_REFERENCE, MODE_TYPE_FILE, Status::NOT_FILE);
        ncvf_err(READ_WRITE, MODE_TYPE_FILE, Status::NOT_FILE);
    }

    #[test]
    fn mode_directory() {
        ncvf_err(OPEN_FLAG_NODE_REFERENCE, MODE_TYPE_DIRECTORY, Status::NOT_DIR);
        ncvf_err(READ_WRITE, MODE_TYPE_DIRECTORY, Status::NOT_DIR);
    }

    #[test]
    fn mode_socket() {
        ncvf_err(OPEN_FLAG_NODE_REFERENCE, MODE_TYPE_SOCKET, Status::INVALID_ARGS);
        ncvf_err(READ_WRITE, MODE_TYPE_SOCKET, Status::INVALID_ARGS);
    }

    #[test]
    fn mode_service() {
        ncvf_ok(OPEN_FLAG_NODE_REFERENCE, MODE_TYPE_SERVICE, OPEN_FLAG_NODE_REFERENCE);
        ncvf_ok(READ_WRITE, MODE_TYPE_SERVICE, READ_WRITE);
    }
}
