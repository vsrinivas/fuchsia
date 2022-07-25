// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities for pseudo file implementations

use {fidl_fuchsia_io as fio, fuchsia_zircon as zx};

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
    mut flags: fio::OpenFlags,
    mut readable: bool,
    mut writable: bool,
    mut executable: bool,
    append_allowed: bool,
) -> Result<fio::OpenFlags, zx::Status> {
    // Nodes supporting both W+X rights are not supported.
    debug_assert!(!(writable && executable));

    if flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
        flags &= fio::OPEN_FLAGS_ALLOWED_WITH_NODE_REFERENCE;
        readable = false;
        writable = false;
        executable = false;
    }

    if flags.intersects(fio::OpenFlags::DIRECTORY) {
        return Err(zx::Status::NOT_DIR);
    }

    if flags.intersects(fio::OpenFlags::NOT_DIRECTORY) {
        flags &= !fio::OpenFlags::NOT_DIRECTORY;
    }

    // For files, the OPEN_FLAG_POSIX_* flags are ignored as they have meaning only for directories.
    flags &= !(fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE);

    let allowed_flags = fio::OpenFlags::NODE_REFERENCE
        | fio::OpenFlags::DESCRIBE
        | fio::OpenFlags::CREATE
        | fio::OpenFlags::CREATE_IF_ABSENT
        | if readable { fio::OpenFlags::RIGHT_READABLE } else { fio::OpenFlags::empty() }
        | if writable {
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::TRUNCATE
        } else {
            fio::OpenFlags::empty()
        }
        | if writable && append_allowed { fio::OpenFlags::APPEND } else { fio::OpenFlags::empty() }
        | if executable { fio::OpenFlags::RIGHT_EXECUTABLE } else { fio::OpenFlags::empty() };

    let prohibited_flags = (fio::OpenFlags::empty() | if readable {
            fio::OpenFlags::TRUNCATE
        } else {
            fio::OpenFlags::empty()
        } | if writable {
            fio::OpenFlags::APPEND
        } else {
            fio::OpenFlags::empty()
        })
        // allowed_flags takes precedence over prohibited_flags.
        & !allowed_flags
        // TRUNCATE is only allowed if the file is writable.
        | if !flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            fio::OpenFlags::TRUNCATE
        } else {
            fio::OpenFlags::empty()
        };

    if !readable && flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if !writable && flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if !executable && flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE) {
        return Err(zx::Status::ACCESS_DENIED);
    }

    if flags.intersects(prohibited_flags) {
        return Err(zx::Status::INVALID_ARGS);
    }

    if flags.intersects(!allowed_flags) {
        return Err(zx::Status::NOT_SUPPORTED);
    }

    Ok(flags)
}

/// Converts the set of validated VMO flags to their respective zx::Rights.
pub fn vmo_flags_to_rights(vmo_flags: fio::VmoFlags) -> zx::Rights {
    // Map VMO flags to their respective rights.
    let mut rights = zx::Rights::NONE;
    if vmo_flags.contains(fio::VmoFlags::READ) {
        rights |= zx::Rights::READ;
    }
    if vmo_flags.contains(fio::VmoFlags::WRITE) {
        rights |= zx::Rights::WRITE;
    }
    if vmo_flags.contains(fio::VmoFlags::EXECUTE) {
        rights |= zx::Rights::EXECUTE;
    }

    rights
}

/// Validate flags passed to `get_backing_memory` against the underlying connection flags.
/// Returns Ok() if the flags were validated, and an Error(zx::Status) otherwise.
///
/// Changing this function can be dangerous!  Flags operations may have security implications.
pub fn get_backing_memory_validate_flags(
    vmo_flags: fio::VmoFlags,
    connection_flags: fio::OpenFlags,
) -> Result<(), zx::Status> {
    // Disallow inconsistent flag combination.
    if vmo_flags.contains(fio::VmoFlags::PRIVATE_CLONE)
        && vmo_flags.contains(fio::VmoFlags::SHARED_BUFFER)
    {
        return Err(zx::Status::INVALID_ARGS);
    }

    // Ensure the requested rights in vmo_flags do not exceed those of the underlying connection.
    if vmo_flags.contains(fio::VmoFlags::READ)
        && !connection_flags.intersects(fio::OpenFlags::RIGHT_READABLE)
    {
        return Err(zx::Status::ACCESS_DENIED);
    }
    if vmo_flags.contains(fio::VmoFlags::WRITE)
        && !connection_flags.intersects(fio::OpenFlags::RIGHT_WRITABLE)
    {
        return Err(zx::Status::ACCESS_DENIED);
    }
    if vmo_flags.contains(fio::VmoFlags::EXECUTE)
        && !connection_flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE)
    {
        return Err(zx::Status::ACCESS_DENIED);
    }

    // As documented in the fuchsia.io interface, if VmoFlags::EXECUTE is requested, ensure that the
    // connection also has OPEN_RIGHT_READABLE.
    if vmo_flags.contains(fio::VmoFlags::EXECUTE)
        && !connection_flags.intersects(fio::OpenFlags::RIGHT_READABLE)
    {
        return Err(zx::Status::ACCESS_DENIED);
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{
        get_backing_memory_validate_flags, new_connection_validate_flags, vmo_flags_to_rights,
    };
    use crate::test_utils::build_flag_combinations;

    use {fidl_fuchsia_io as fio, fuchsia_zircon as zx};

    fn io_flags_to_rights(flags: fio::OpenFlags) -> (bool, bool, bool) {
        return (
            flags.intersects(fio::OpenFlags::RIGHT_READABLE),
            flags.intersects(fio::OpenFlags::RIGHT_WRITABLE),
            flags.intersects(fio::OpenFlags::RIGHT_EXECUTABLE),
        );
    }

    fn rights_to_vmo_flags(readable: bool, writable: bool, executable: bool) -> fio::VmoFlags {
        return if readable { fio::VmoFlags::READ } else { fio::VmoFlags::empty() }
            | if writable { fio::VmoFlags::WRITE } else { fio::VmoFlags::empty() }
            | if executable { fio::VmoFlags::EXECUTE } else { fio::VmoFlags::empty() };
    }

    #[track_caller]
    fn ncvf_ok(
        flags: fio::OpenFlags,
        readable: bool,
        writable: bool,
        executable: bool,
        expected_new_flags: fio::OpenFlags,
    ) {
        assert!(!(writable && executable), "Cannot specify both writable and executable!");
        let res = new_connection_validate_flags(
            flags, readable, writable, executable, /*append_allowed=*/ false,
        );
        match res {
            Ok(new_flags) => assert_eq!(
                expected_new_flags, new_flags,
                "new_connection_validate_flags returned unexpected set of flags.\n\
                    Expected: {:?}\n\
                    Actual: {:?}",
                expected_new_flags, new_flags
            ),
            Err(status) => panic!("new_connection_validate_flags failed.  Status: {}", status),
        }
    }

    #[track_caller]
    fn ncvf_err(
        flags: fio::OpenFlags,
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
        let preserved_flags = fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DESCRIBE;
        for open_flags in build_flag_combinations(
            fio::OpenFlags::NODE_REFERENCE.bits(),
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DESCRIBE)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
            ncvf_ok(open_flags, true, true, false, open_flags & preserved_flags);
        }

        ncvf_err(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::DIRECTORY,
            true,
            true,
            false,
            zx::Status::NOT_DIR,
        );

        ncvf_ok(
            fio::OpenFlags::NODE_REFERENCE | fio::OpenFlags::NOT_DIRECTORY,
            true,
            true,
            false,
            fio::OpenFlags::NODE_REFERENCE,
        );
    }

    #[test]
    fn new_connection_validate_flags_posix() {
        // OPEN_FLAG_POSIX_* is ignored for files.
        const ALL_POSIX_FLAGS: fio::OpenFlags = fio::OpenFlags::empty()
            .union(fio::OpenFlags::POSIX_WRITABLE)
            .union(fio::OpenFlags::POSIX_EXECUTABLE);
        for open_flags in build_flag_combinations(
            0,
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE
                | ALL_POSIX_FLAGS)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            // Skip disallowed W+X combinations, and skip combinations without any POSIX flags.
            if (writable && executable) || !open_flags.intersects(ALL_POSIX_FLAGS) {
                continue;
            }
            ncvf_ok(open_flags, readable, writable, executable, open_flags & !ALL_POSIX_FLAGS);
        }
    }

    #[test]
    fn new_connection_validate_flags_create() {
        for open_flags in build_flag_combinations(
            fio::OpenFlags::CREATE.bits(),
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::CREATE_IF_ABSENT)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            ncvf_ok(open_flags, readable, writable, executable, open_flags);
        }
    }

    #[test]
    fn new_connection_validate_flags_truncate() {
        ncvf_err(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::TRUNCATE,
            true,
            true,
            false,
            zx::Status::INVALID_ARGS,
        );
        ncvf_ok(
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::TRUNCATE,
            true,
            true,
            false,
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::TRUNCATE,
        );
        ncvf_err(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::TRUNCATE,
            true,
            false,
            false,
            zx::Status::INVALID_ARGS,
        );
    }

    #[test]
    fn new_connection_validate_flags_append() {
        ncvf_err(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::APPEND,
            true,
            false,
            false,
            zx::Status::NOT_SUPPORTED,
        );
        ncvf_err(
            fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::APPEND,
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
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
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
        for vmo_flags in build_flag_combinations(
            0,
            (fio::VmoFlags::READ | fio::VmoFlags::WRITE | fio::VmoFlags::EXECUTE).bits(),
        ) {
            let vmo_flags = fio::VmoFlags::from_bits_truncate(vmo_flags.into());
            let rights: zx::Rights = vmo_flags_to_rights(vmo_flags);
            assert_eq!(vmo_flags.contains(fio::VmoFlags::READ), rights.contains(zx::Rights::READ));
            assert_eq!(
                vmo_flags.contains(fio::VmoFlags::WRITE),
                rights.contains(zx::Rights::WRITE)
            );
            assert_eq!(
                vmo_flags.contains(fio::VmoFlags::EXECUTE),
                rights.contains(zx::Rights::EXECUTE)
            );
        }
    }

    #[test]
    fn get_backing_memory_validate_flags_invalid() {
        // Cannot specify both PRIVATE and EXACT at the same time, since they conflict.
        assert_eq!(
            get_backing_memory_validate_flags(
                fio::VmoFlags::PRIVATE_CLONE | fio::VmoFlags::SHARED_BUFFER,
                fio::OpenFlags::empty()
            ),
            Err(zx::Status::INVALID_ARGS)
        );
    }

    /// Ensure that the check passes if we request the same or less rights than the connection has.
    #[test]
    fn get_backing_memory_validate_flags_less_rights() {
        for open_flags in build_flag_combinations(
            0,
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            let vmo_flags = rights_to_vmo_flags(readable, writable, executable);

            // The io1.fidl protocol specifies that VmoFlags::EXECUTE requires the connection to be
            // both readable and executable.
            if executable && !readable {
                assert_eq!(
                    get_backing_memory_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
                continue;
            }

            // Ensure that we can open the VMO with the same rights as the connection.
            get_backing_memory_validate_flags(vmo_flags, open_flags)
                .expect("Failed to validate flags");

            // Ensure that we can also open the VMO with *less* rights than the connection has.
            if readable {
                let vmo_flags = rights_to_vmo_flags(false, writable, false);
                get_backing_memory_validate_flags(vmo_flags, open_flags)
                    .expect("Failed to validate flags");
            }
            if writable {
                let vmo_flags = rights_to_vmo_flags(readable, false, executable);
                get_backing_memory_validate_flags(vmo_flags, open_flags)
                    .expect("Failed to validate flags");
            }
            if executable {
                let vmo_flags = rights_to_vmo_flags(true, writable, false);
                get_backing_memory_validate_flags(vmo_flags, open_flags)
                    .expect("Failed to validate flags");
            }
        }
    }

    /// Ensure that vmo_flags cannot exceed rights of connection_flags.
    #[test]
    fn get_backing_memory_validate_flags_more_rights() {
        for open_flags in build_flag_combinations(
            0,
            (fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::RIGHT_EXECUTABLE)
                .bits(),
        ) {
            let open_flags = fio::OpenFlags::from_bits_truncate(open_flags);
            // Ensure we cannot return a VMO with more rights than the connection itself has.
            let (readable, writable, executable) = io_flags_to_rights(open_flags);
            if !readable {
                let vmo_flags = rights_to_vmo_flags(true, writable, executable);
                assert_eq!(
                    get_backing_memory_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
            if !writable {
                let vmo_flags = rights_to_vmo_flags(readable, true, false);
                assert_eq!(
                    get_backing_memory_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
            if !executable {
                let vmo_flags = rights_to_vmo_flags(readable, false, true);
                assert_eq!(
                    get_backing_memory_validate_flags(vmo_flags, open_flags),
                    Err(zx::Status::ACCESS_DENIED)
                );
            }
        }
    }
}
