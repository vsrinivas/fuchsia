// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{future, pin_mut, StreamExt},
    log::warn,
};

use crate::{config::AudioGatewayFeatureSupport, hfp::Hfp, profile::Profile};

mod config;
mod error;
mod hfp;
mod profile;
mod service_definitions;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().context("Could not initialize logger")?;

    let feature_support = AudioGatewayFeatureSupport::load()?;
    let profile = Profile::register_audio_gateway(feature_support)?;
    let hfp = Hfp::new(profile).run();
    pin_mut!(hfp);

    let mut fs = ServiceFs::new();

    let inspector = fuchsia_inspect::Inspector::new();
    if let Err(e) = inspector.serve(&mut fs) {
        warn!("Could not serve inspect: {}", e);
    }

    fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    let fs = fs.collect::<()>();

    future::select(fs, hfp).await;

    Ok(())
}
