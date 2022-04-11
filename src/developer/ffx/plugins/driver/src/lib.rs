// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_driver_args::DriverCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol,
};

#[ffx_plugin("driver")]
pub async fn driver(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: DriverCommand,
) -> Result<()> {
    driver_tools::driver(remote_control, cmd.into()).await
}
