// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities for pseudo file implementations

use {
    fidl_fuchsia_io::{
        MODE_PROTECTION_MASK, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_APPEND,
        OPEN_FLAG_DESCRIBE, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_POSIX,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
};

/// Validate that the requested flags for a new connection are valid. This validates flags against
/// the flags of the parent connection, as well as whether or not the pseudo file is readable or
/// writable at all (e.g. if an on_read or on_write function was provided, respectively). On success,
/// it returns the validated flags, with some ambiguities cleaned up. On failure, it returns a
/// [`Status`] indicating the problem.
///
/// Changing this function can be dangerous! Not only does it have obvious security implications, but
/// connections currently rely on it to reject unsupported functionality, such as attempting to read
/// from a file when `on_read` is `None`.
pub fn new_connection_validate_flags(
    mut flags: u32,
    mode: u32,
    readable: bool,
    writable: bool,
) -> Result<u32, Status> {
    // There should be no MODE_TYPE_* flags set, except for, possibly, MODE_TYPE_FILE when the
    // target is a pseudo file.
    if (mode & !MODE_PROTECTION_MASK) & !MODE_TYPE_FILE != 0 {
        if (mode & !MODE_PROTECTION_MASK) & MODE_TYPE_DIRECTORY != 0 {
            return Err(Status::NOT_DIR);
        } else {
            return Err(Status::INVALID_ARGS);
        };
    }

    if flags & OPEN_FLAG_NODE_REFERENCE != 0 {
        flags &= !OPEN_FLAG_NODE_REFERENCE;
        flags &= OPEN_FLAG_DIRECTORY | OPEN_FLAG_DESCRIBE;
    }

    if flags & OPEN_FLAG_DIRECTORY != 0 {
        return Err(Status::NOT_DIR);
    }

    // For files OPEN_FLAG_POSIX is just ignored, as it has meaning only for directories.
    flags &= !OPEN_FLAG_POSIX;

    let allowed_flags = OPEN_FLAG_DESCRIBE
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
