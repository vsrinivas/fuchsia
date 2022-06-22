// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use anyhow::{Context as _, Error};
use fidl_fuchsia_update_config::{
    OptOutAdminMarker, OptOutAdminSynchronousProxy, OptOutMarker, OptOutSynchronousProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;

#[fasync::run_singlethreaded(test)]
async fn test_basic() -> Result<(), Error> {
    // Initialize a channel, and label the two ends as the server_end and client_end
    let (server_end, client_end) = zx::Channel::create()?;
    let (admin_server_end, admin_client_end) = zx::Channel::create()?;

    // Connect an implementation of the OptOut protocols to the server ends
    connect_channel_to_protocol::<OptOutMarker>(server_end)
        .context("Failed to connect to OTA OptOut service")?;
    connect_channel_to_protocol::<OptOutAdminMarker>(admin_server_end)
        .context("Failed to connect to OTA OptOut service")?;

    // Create a synchronous proxy using the client end
    let optout_svc = OptOutSynchronousProxy::new(client_end);
    let optout_admin_svc = OptOutAdminSynchronousProxy::new(admin_client_end);

    // get a current value.
    let value = optout_svc.get(zx::Time::INFINITE)?;

    // set the value back to check that OptOutAdmin protocol works
    let res = optout_admin_svc.set(value, zx::Time::INFINITE)?;
    assert_eq!(res, Ok(()));

    Ok(())
}
