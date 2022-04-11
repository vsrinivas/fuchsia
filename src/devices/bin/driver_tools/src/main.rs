// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error, Result},
    driver_tools::args::DriverCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fuchsia_async,
    fuchsia_component::client,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let command: DriverCommand = argh::from_env();
    let remote_control = client::connect_to_protocol::<fremotecontrol::RemoteControlMarker>()
        .context("Failed to connect to remote control service")?;
    driver_tools::driver(remote_control, command).await
}
