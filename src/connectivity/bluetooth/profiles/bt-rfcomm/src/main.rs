// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use anyhow::{Context as _, Error};
use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect_derive::Inspect;
use futures::{self, channel::mpsc, future, pin_mut};
use tracing::warn;

mod fidl_service;
mod profile;
mod profile_registrar;
mod rfcomm;
mod types;

use crate::fidl_service::run_services;
use crate::profile_registrar::ProfileRegistrar;

#[fuchsia_async::run_singlethreaded]
pub async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-rfcomm"]).expect("unable to initialize logger");
    let profile_svc = fuchsia_component::client::connect_to_protocol::<ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;

    let (service_sender, service_receiver) = mpsc::channel(1);

    let mut fs = ServiceFs::new();

    let inspect = fuchsia_inspect::Inspector::new();
    if let Err(e) = inspect_runtime::serve(&inspect, &mut fs) {
        warn!("Could not serve inspect: {}", e);
    }
    let services = run_services(fs, service_sender)?;
    pin_mut!(services);

    let mut profile_registrar = ProfileRegistrar::new(profile_svc);
    if let Err(e) = profile_registrar.iattach(inspect.root(), "rfcomm_server") {
        warn!("Failed to attach to inspect: {}", e);
    }
    let profile_registrar_fut = profile_registrar.start(service_receiver);

    match future::select(services, profile_registrar_fut).await {
        future::Either::Left(((), _)) => {
            warn!("Service FS directory handle closed. Exiting.");
        }
        future::Either::Right(((), _)) => {
            warn!("All Profile related connections have terminated. Exiting.");
        }
    }

    Ok(())
}
