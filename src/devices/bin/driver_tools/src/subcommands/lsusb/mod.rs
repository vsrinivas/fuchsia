// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common, anyhow::Result, args::LsusbCommand,
    fidl_fuchsia_developer_remotecontrol as fremotecontrol, fidl_fuchsia_device_manager as fdm,
};

pub async fn lsusb(
    remote_control: fremotecontrol::RemoteControlProxy,
    cmd: LsusbCommand,
) -> Result<()> {
    let device_watcher = common::remotecontrol_connect::<fdm::DeviceWatcherMarker>(
        &remote_control,
        "bootstrap/driver_manager:expose:fuchsia.hardware.usb.DeviceWatcher",
    )
    .await?;
    lsusb::lsusb(device_watcher, cmd.into()).await
}
