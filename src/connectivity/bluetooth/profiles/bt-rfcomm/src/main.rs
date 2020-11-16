// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect_derive::Inspect,
    futures::{self, channel::mpsc, future, sink::SinkExt, stream::StreamExt},
    log::warn,
};

mod profile;
mod profile_registrar;
mod rfcomm;
mod types;

use crate::profile_registrar::ProfileRegistrar;

#[fasync::run_singlethreaded]
pub async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-rfcomm"]).expect("Can't init logger");

    let profile_svc = fuchsia_component::client::connect_to_service::<ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;

    let (sender, receiver) = mpsc::channel(0);
    let mut clients = Vec::new();

    let mut fs = ServiceFs::new();

    let inspect = fuchsia_inspect::Inspector::new();
    if let Err(e) = inspect.serve(&mut fs) {
        warn!("Could not serve inspect: {}", e);
    }

    fs.dir("svc").add_fidl_service(move |stream| {
        let mut stream_sender = sender.clone();
        let task = fasync::Task::spawn(async move {
            let _ = stream_sender.send(stream).await;
        });
        clients.push(task);
    });
    fs.take_and_serve_directory_handle()?;
    let mut drive_service_fs = fs.collect::<()>();

    let mut profile_registrar = ProfileRegistrar::new(profile_svc);
    if let Err(e) = profile_registrar.iattach(inspect.root(), "rfcomm_server") {
        warn!("Failed to attach to inspect: {}", e);
    }
    let mut profile_registrar_fut = profile_registrar.start(receiver);

    let _ = future::select(&mut profile_registrar_fut, &mut drive_service_fs).await;

    Ok(())
}
