// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use tracing::{debug, info};

mod config;
mod error;
mod gatt_service;
mod host_watcher;
mod low_energy_advertiser;
mod types;

use config::Config;
use gatt_service::GattService;
use low_energy_advertiser::LowEnergyAdvertiser;

#[fuchsia::main(logging_tags = ["bt-fastpair-provider"])]
async fn main() -> Result<(), Error> {
    let provider_config = Config::load()?;
    debug!("Starting Fast Pair Provider: {:?}", provider_config);

    // TODO(fxbug.dev/95542): Create the GATT service and LE Advertiser in the toplevel FP Provider
    // server.
    let _gatt_service = GattService::new(provider_config).await?;
    let _advertiser = LowEnergyAdvertiser::new()?;

    info!("Fast Pair Provider component running.");
    Ok(())
}
