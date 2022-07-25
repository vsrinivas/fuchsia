// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Code shared between several modules of the service implementation.

use {
    fidl_fuchsia_io as fio,
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
pub fn new_connection_validate_flags(
    mut flags: fio::OpenFlags,
    mode: u32,
) -> Result<fio::OpenFlags, Status> {
    // There should be no MODE_TYPE_* flags set, except for, possibly, MODE_TYPE_SERVICE when the
    // target is a service.
    if (mode & !fio::MODE_PROTECTION_MASK) & !fio::MODE_TYPE_SERVICE != 0 {
        if mode & fio::MODE_TYPE_MASK == fio::MODE_TYPE_DIRECTORY {
            return Err(Status::NOT_DIR);
        } else if mode & fio::MODE_TYPE_MASK == fio::MODE_TYPE_FILE {
            return Err(Status::NOT_FILE);
        } else {
            return Err(Status::INVALID_ARGS);
        };
    }

    // A service is not a directory.
    flags &= !fio::OpenFlags::NOT_DIRECTORY;

    // For services any OPEN_FLAG_POSIX_* flags are ignored as they only apply to directories.
    flags &= !(fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE);

    if flags.intersects(fio::OpenFlags::DIRECTORY) {
        return Err(Status::NOT_DIR);
    }

    if flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
        flags &= !(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        if flags.intersects(!fio::OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE) {
            return Err(Status::INVALID_ARGS);
        }
        flags &= fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE;
        return Ok(flags);
    }

    // All the flags we have already checked above and removed.
    debug_assert!(!flags.intersects(
        fio::OpenFlags::DIRECTORY
            | fio::OpenFlags::NOT_DIRECTORY
            | fio::OpenFlags::POSIX_WRITABLE
            | fio::OpenFlags::POSIX_EXECUTABLE
            | fio::OpenFlags::NODE_REFERENCE
    ));

    // A service might only be connected to when read permissions are present.
    if !flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
        return Err(Status::ACCESS_DENIED);
    }
    let allowed_flags =
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DESCRIBE;

    // Anything else is also not allowed.
    if flags.intersects(!allowed_flags) {
        return Err(Status::INVALID_ARGS);
    }

    Ok(flags)
}

#[cfg(test)]
mod tests {
    use super::new_connection_validate_flags;

    use {fidl_fuchsia_io as fio, fuchsia_zircon::Status};

    /// Assertion for when `new_connection_validate_flags` should succeed => `ncvf_ok`.
    fn ncvf_ok(flags: fio::OpenFlags, mode: u32, expected_new_flags: fio::OpenFlags) {
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
    fn ncvf_err(flags: fio::OpenFlags, mode: u32, expected_status: Status) {
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
    const READ_WRITE: fio::OpenFlags = fio::OpenFlags::empty()
        .union(fio::OpenFlags::RIGHT_READABLE)
        .union(fio::OpenFlags::RIGHT_WRITABLE);

    #[test]
    fn node_reference_basic() {
        // OPEN_FLAG_NODE_REFERENCE is preserved.
        ncvf_ok(fio::OpenFlags::NODE_REFERENCE, 0, fio::OpenFlags::NODE_REFERENCE);

        // Access flags are dropped.
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::RIGHT_READABLE,
            0,
            fio::OpenFlags::NODE_REFERENCE,
        );
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            fio::OpenFlags::NODE_REFERENCE,
        );

        // OPEN_FLAG_DESCRIBE is preserved.
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE,
            0,
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE,
        );
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | READ_WRITE | fio::OpenFlags::DESCRIBE,
            0,
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE,
        );

        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::NOT_DIRECTORY,
            0,
            fio::OpenFlags::NODE_REFERENCE,
        );
        ncvf_err(fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DIRECTORY, 0, Status::NOT_DIR);
    }

    #[test]
    fn service_basic() {
        // Access flags are required and preserved.
        ncvf_ok(READ_WRITE, 0, READ_WRITE);
        ncvf_ok(fio::OpenFlags::RIGHT_READABLE, 0, fio::OpenFlags::RIGHT_READABLE);
        ncvf_err(fio::OpenFlags::RIGHT_WRITABLE, 0, Status::ACCESS_DENIED);

        // OPEN_FLAG_DESCRIBE is allowed.
        ncvf_ok(READ_WRITE | fio::OpenFlags::DESCRIBE, 0, READ_WRITE | fio::OpenFlags::DESCRIBE);

        ncvf_ok(READ_WRITE | fio::OpenFlags::NOT_DIRECTORY, 0, READ_WRITE);
        ncvf_err(READ_WRITE | fio::OpenFlags::DIRECTORY, 0, Status::NOT_DIR);
    }

    #[test]
    fn node_reference_posix() {
        // OPEN_FLAG_POSIX_* is ignored for services.
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            fio::OpenFlags::NODE_REFERENCE,
        );
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE
                | fio::OpenFlags::DESCRIBE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE,
        );
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE
                | READ_WRITE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            fio::OpenFlags::NODE_REFERENCE,
        );
    }

    #[test]
    fn service_posix() {
        // OPEN_FLAG_POSIX_* is ignored for services.
        ncvf_ok(
            READ_WRITE | fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            READ_WRITE,
        );
        ncvf_ok(
            READ_WRITE
                | fio::OpenFlags::DESCRIBE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            READ_WRITE | fio::OpenFlags::DESCRIBE,
        );
    }

    #[test]
    fn file() {
        ncvf_err(fio::OpenFlags::NODE_REFERENCE, fio::MODE_TYPE_FILE, Status::NOT_FILE);
        ncvf_err(READ_WRITE, fio::MODE_TYPE_FILE, Status::NOT_FILE);
    }

    #[test]
    fn mode_directory() {
        ncvf_err(fio::OpenFlags::NODE_REFERENCE, fio::MODE_TYPE_DIRECTORY, Status::NOT_DIR);
        ncvf_err(READ_WRITE, fio::MODE_TYPE_DIRECTORY, Status::NOT_DIR);
    }

    #[test]
    fn mode_service() {
        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE,
            fio::MODE_TYPE_SERVICE,
            fio::OpenFlags::NODE_REFERENCE,
        );
        ncvf_ok(READ_WRITE, fio::MODE_TYPE_SERVICE, READ_WRITE);
    }
}
