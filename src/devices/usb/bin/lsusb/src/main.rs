// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, fdio, fidl_fuchsia_device_manager::DeviceWatcherMarker, fuchsia_async,
    lsusb::args::Args,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<()> {
    let args: Args = argh::from_env();
    let (proxy, server) = fidl::endpoints::create_proxy::<DeviceWatcherMarker>()?;

    fdio::service_connect("/svc/fuchsia.hardware.usb.DeviceWatcher", server.into_channel())?;
    lsusb::lsusb(proxy, args).await
}
