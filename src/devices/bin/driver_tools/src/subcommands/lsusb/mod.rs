// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {anyhow::Result, args::LsusbCommand, fidl_fuchsia_device_manager as fdm};

pub async fn lsusb(cmd: LsusbCommand, device_watcher_proxy: fdm::DeviceWatcherProxy) -> Result<()> {
    lsusb::lsusb(device_watcher_proxy, cmd.into()).await
}
