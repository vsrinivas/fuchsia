// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{self, channel::mpsc, future, sink::SinkExt, stream::StreamExt},
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

    let mut profile_registrar_fut = ProfileRegistrar::start(profile_svc, receiver);

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let mut stream_sender = sender.clone();
        let task = fasync::Task::spawn(async move {
            let _ = stream_sender.send(stream).await;
        });
        clients.push(task);
    });
    fs.take_and_serve_directory_handle()?;
    let mut drive_service_fs = fs.collect::<()>();

    let _ = future::select(&mut profile_registrar_fut, &mut drive_service_fs).await;

    Ok(())
}
