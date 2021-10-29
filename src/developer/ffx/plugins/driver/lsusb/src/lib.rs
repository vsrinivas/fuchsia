// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_driver_lsusb_args::DriverLsusbCommand,
    fidl_fuchsia_device_manager::DeviceWatcherProxy,
};

#[ffx_plugin(
    "driver_enabled",
    DeviceWatcherProxy = "bootstrap/driver_manager:expose:fuchsia.hardware.usb.DeviceWatcher"
)]
pub async fn lsusb(device_watcher: DeviceWatcherProxy, cmd: DriverLsusbCommand) -> Result<()> {
    lsusb::lsusb(device_watcher, cmd.into()).await
}
