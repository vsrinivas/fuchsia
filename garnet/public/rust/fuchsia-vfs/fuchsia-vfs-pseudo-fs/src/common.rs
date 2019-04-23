// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by both directory and file traits.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{NodeMarker, OPEN_FLAG_DESCRIBE},
    fuchsia_zircon::Status,
};

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
    if flags & OPEN_FLAG_DESCRIBE == 0 {
        return;
    }

    if status == Status::OK {
        panic!("send_on_open_with_error() should not be used to respond with Status::OK");
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
