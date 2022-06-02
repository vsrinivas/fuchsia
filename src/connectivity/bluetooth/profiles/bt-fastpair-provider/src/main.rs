// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use tracing::{debug, info, warn};

mod advertisement;
mod config;
mod error;
mod gatt_service;
mod host_watcher;
mod keys;
mod packets;
mod provider;
mod types;

use config::Config;
use provider::Provider;

#[fuchsia::main(logging_tags = ["bt-fastpair-provider"])]
async fn main() -> Result<(), Error> {
    let provider_config = Config::load()?;
    debug!("Starting Fast Pair Provider: {:?}", provider_config);

    let server = Provider::new(provider_config).await?;
    let fast_pair = server.run();
    info!("Fast Pair Provider component running.");

    if let Err(e) = fast_pair.await {
        warn!("Error in Fast Pair Provider main loop: {:?}", e);
    }

    Ok(())
}
