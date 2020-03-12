// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Dummy Driver

use anyhow::{Context as _, Error};
use fidl_fuchsia_lowpan_device::RegisterMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_syslog::macros::*;
use lowpan_driver_common::{register_and_serve_driver, DummyDevice};
use std::default::Default;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan_dummy_driver"]).context("initialize logging")?;

    let name = "lowpan0";

    let device = DummyDevice::default();

    fx_log_info!("Connecting to LoWPAN service");

    let registry = connect_to_service::<RegisterMarker>()
        .context("Failed to connect to LoWPAN Registry service")?;

    register_and_serve_driver(name, registry, &device)
        .await
        .context("Error serving LoWPAN device")?;

    fx_log_info!("Dummy LoWPAN device {:?} has shutdown.", name);

    Ok(())
}
