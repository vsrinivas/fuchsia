// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![recursion_limit = "1024"]

use {
    anyhow::{Context, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{future, pin_mut, StreamExt},
    log::warn,
};

use crate::{
    call_manager::CallManager,
    config::AudioGatewayFeatureSupport,
    fidl_service::{handle_hfp_client_connection, Services},
    hfp::Hfp,
    profile::Profile,
};

mod at;
mod call_manager;
mod config;
mod error;
mod fidl_service;
mod hfp;
mod peer;
mod profile;
mod protocol;
mod service_definitions;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init().context("Could not initialize logger")?;

    let feature_support = AudioGatewayFeatureSupport::load()?;
    let profile = Profile::register_audio_gateway(feature_support)?;
    let mut call_manager = CallManager::new();
    let service_provider =
        call_manager.service_provider().expect("A valid call manager service provider");
    let hfp = Hfp::new(profile, call_manager, feature_support).run();
    pin_mut!(hfp);

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(Services::Hfp);

    let inspector = fuchsia_inspect::Inspector::new();
    if let Err(e) = inspector.serve(&mut fs) {
        warn!("Could not serve inspect: {}", e);
    }

    fs.take_and_serve_directory_handle().context("Failed to serve ServiceFs directory")?;
    let fs = fs.for_each_concurrent(10_000, move |Services::Hfp(stream)| {
        handle_hfp_client_connection(stream, service_provider.clone())
    });
    pin_mut!(fs);

    match future::select(fs, hfp).await {
        future::Either::Left(((), _)) => {
            log::warn!("Service FS directory handle closed. Exiting.");
        }
        future::Either::Right((Ok(()), _)) => {
            log::warn!(
                "All Hfp related connections to this component have been disconnected. Exiting."
            );
        }
        future::Either::Right((Err(e), _)) => {
            log::warn!("Error encountered running main Hfp loop: {}. Exiting.", e);
        }
    }

    Ok(())
}
