// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_device_manager::BindDebuggerMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    /// The path of the driver to debug, e.g. "/boot/driver/platform-bus.so"
    driver_path: String,

    /// The topological path of the device to debug, e.g. "sys/pci/00:1f.6"
    device_path: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_iter(std::env::args());

    let service = connect_to_service::<BindDebuggerMarker>()
        .context("Failed to connect to bind debugger service")?;

    let bind_program =
        service.get_bind_program(&opt.driver_path).await.context("Failed to get bind program")?;
    let device_properties = service
        .get_device_properties(&opt.device_path)
        .await
        .context("Failed to get device properties")?;

    println!("Bind program: {:#?}", bind_program);
    println!("Device properties: {:#?}", device_properties);

    Ok(())
}
