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
/// # Panics
/// If `status` is `Status::OK`.  In this case `OnOpen` may need to contain a description of the
/// object, and server_end should not be droppped.
pub fn send_on_open_with_error(
    flags: u32,
    server_end: ServerEnd<NodeMarker>,
    status: Status,
) -> Result<(), fidl::Error> {
    if flags & OPEN_FLAG_DESCRIBE == 0 {
        return Ok(());
    }

    if status == Status::OK {
        panic!("send_on_open_with_error() should not be used to respond with Status::OK");
    }

    let (_, control_handle) = server_end.into_stream_and_control_handle()?;
    control_handle.send_on_open_(status.into_raw(), None)
}
