// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_device_manager::DriverHostDevelopmentMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    /// Restart Driver Hosts containing the driver specified by `driver_path`.
    driver_path: String,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get driver path as argument.
    let opt = Opt::from_iter(std::env::args());

    // Connect to the DriverHostDevelopment Protocol service.
    let service = connect_to_service::<DriverHostDevelopmentMarker>()
        .context("Failed to connect to development service")?;

    // Make request to devcoordinator to restart driver hosts.
    let res = service.restart_driver_hosts(&opt.driver_path).await?;
    println!("{:?}", res);

    Ok(())
}
