// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fdio, fidl_fuchsia_boot as fboot, fuchsia_component,
    fuchsia_zircon::{self as zx, HandleBased},
};

pub async fn init() -> Result<(), Error> {
    let write_only_log_proxy =
        fuchsia_component::client::connect_to_protocol::<fboot::WriteOnlyLogMarker>()?;

    let debuglog_handle = write_only_log_proxy.get().await?;

    for fd in &[1, 2] {
        let debuglog_dup = debuglog_handle.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        fdio::bind_to_fd(debuglog_dup.into_handle(), *fd)?;
    }
    Ok(())
}
