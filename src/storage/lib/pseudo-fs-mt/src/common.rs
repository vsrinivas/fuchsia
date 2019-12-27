// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by both directory and file traits.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeMarker, CLONE_FLAG_SAME_RIGHTS, OPEN_FLAG_APPEND, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_NODE_REFERENCE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
};

/// Set of known rights.
const FS_RIGHTS: u32 = OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_RIGHT_ADMIN;

/// Returns true if the rights flags in `flags` do not exceed those in `parent_flags`.
pub fn stricter_or_same_rights(parent_flags: u32, flags: u32) -> bool {
    let parent_rights = parent_flags & FS_RIGHTS;
    let rights = flags & FS_RIGHTS;
    return (rights & !parent_rights) == 0;
}

/// Common logic for rights processing during cloning a node, shared by both file and directory
/// implementations.
pub fn inherit_rights_for_clone(parent_flags: u32, mut flags: u32) -> Result<u32, Status> {
    if (flags & CLONE_FLAG_SAME_RIGHTS != 0) && (flags & FS_RIGHTS != 0) {
        return Err(Status::INVALID_ARGS);
    }

    // We preserve OPEN_FLAG_APPEND as this is what is the most convenient for the POSIX emulation.
    //
    // OPEN_FLAG_NODE_REFERENCE is enforced, according to our current FS permissions design.
    flags |= parent_flags & (OPEN_FLAG_APPEND | OPEN_FLAG_NODE_REFERENCE);

    // If CLONE_FLAG_SAME_RIGHTS is requested, cloned connection will inherit the same rights
    // as those from the originating connection.  We have ensured that no FS_RIGHTS flags are set
    // above.
    if flags & CLONE_FLAG_SAME_RIGHTS != 0 {
        flags &= !CLONE_FLAG_SAME_RIGHTS;
        flags |= parent_flags & FS_RIGHTS;
    }

    if !stricter_or_same_rights(parent_flags, flags) {
        return Err(Status::ACCESS_DENIED);
    }

    Ok(flags)
}

/// A helper method to send OnOpen event on the handle owned by the `server_end` in case `flags`
/// contains `OPEN_FLAG_STATUS`.
///
/// If the send operation fails for any reason, the error is ignored.  This helper is used during
/// an Open() or a Clone() FIDL methods, and these methods have no means to propagate errors to the
/// caller.  OnOpen event is the only way to do that, so there is nowhere to report errors in
/// OnOpen dispatch.  `server_end` will be closed, so there will be some kind of indication of the
/// issue.
///
/// TODO Maybe send an epitaph on the `server_end`?
///
/// # Panics
/// If `status` is `Status::OK`.  In this case `OnOpen` may need to contain a description of the
/// object, and server_end should not be droppped.
pub fn send_on_open_with_error(flags: u32, server_end: ServerEnd<NodeMarker>, status: Status) {
    if status == Status::OK {
        panic!("send_on_open_with_error() should not be used to respond with Status::OK");
    }

    if flags & OPEN_FLAG_DESCRIBE == 0 {
        return;
    }

    match server_end.into_stream_and_control_handle() {
        Ok((_, control_handle)) => {
            // There is no reasonable way to report this error.  Assuming the `server_end` has just
            // disconnected or failed in some other way why we are trying to send OnOpen.
            let _ = control_handle.send_on_open_(status.into_raw(), None);
        }
        Err(_) => {
            // Same as above, ignore the error.
            return;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::inherit_rights_for_clone;

    use fidl_fuchsia_io::OPEN_FLAG_NODE_REFERENCE;

    // TODO This should be converted into a function as soon as backtrace support is in place.
    // The only reason this is a macro is to generate error messages that point to the test
    // function source location in their top stack frame.
    macro_rules! irfc_ok {
        ($parent_flags:expr, $flags:expr, $expected_new_flags:expr $(,)*) => {{
            let res = inherit_rights_for_clone($parent_flags, $flags);
            match res {
                Ok(new_flags) => assert_eq!(
                    $expected_new_flags, new_flags,
                    "`inherit_rights_for_clone` returned unexpected set of flags.\n\
                     Expected: {:X}\n\
                     Actual: {:X}",
                    $expected_new_flags, new_flags
                ),
                Err(status) => panic!("`inherit_rights_for_clone` failed.  Status: {}", status),
            }
        }};
    }

    #[test]
    fn node_reference_is_inherited() {
        irfc_ok!(OPEN_FLAG_NODE_REFERENCE, 0, OPEN_FLAG_NODE_REFERENCE);
    }
}
