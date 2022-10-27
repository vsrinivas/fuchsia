// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]
#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]
#![allow(clippy::type_complexity)]

//! Component that implements the fuchsia.update.config.OptOut and
//! fuchsia.update.config.OptOutAdmin protocols.

use anyhow::{Context, Error};
use fuchsia_component::server::ServiceFs;
use tracing::{error, info};

pub mod bridge;
mod health;
mod service;

/// main() entry point to this binary that is actually a library aggregated with other binaries in
/// the //src/sys/pkg/bin/grand-swd-binary.
#[fuchsia::main(logging = true)]
pub async fn main() -> Result<(), Error> {
    info!("starting system-update-configurator");

    if let Err(e) = run().await {
        error!("error running system-update-configurator: {e:#}")
    }

    info!("shutting down system-update-configurator");
    Ok(())
}

async fn run() -> Result<(), Error> {
    let inspector = fuchsia_inspect::Inspector::new();
    let health_status = health::HealthStatus::new(inspector.root());

    let mut fs: service::Fs = ServiceFs::new_local();
    let () = inspect_runtime::serve(&inspector, &mut fs).context("while configuring inspect")?;
    fs.take_and_serve_directory_handle().context("while taking outdir handle")?;

    let storage = bridge::OptOutStorage;

    let mut storage = health_status.wrap_bridge(storage);

    service::serve(fs, &mut storage).await;

    Ok(())
}
