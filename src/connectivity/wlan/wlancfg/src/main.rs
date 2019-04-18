// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]
#![recursion_limit = "256"]

mod client;
mod config;
mod device;
mod known_ess_store;
mod shim;
mod state_machine;

use crate::{config::Config, known_ess_store::KnownEssStore};

use failure::{format_err, Error, ResultExt};
use fidl_fuchsia_wlan_device_service::DeviceServiceMarker;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use std::sync::Arc;
use void::Void;

async fn serve_fidl(
    _client_ref: shim::ClientRef,
    ess_store: Arc<KnownEssStore>,
) -> Result<Void, Error> {
    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(move |stream| {
        let fut = shim::serve_legacy(stream, _client_ref.clone(), Arc::clone(&ess_store))
            .unwrap_or_else(|e| eprintln!("error serving legacy wlan API: {}", e));
        fasync::spawn(fut)
    });
    fs.take_and_serve_directory_handle()?;
    let () = await!(fs.collect());
    Err(format_err!("FIDL server future exited unexpectedly"))
}

fn main() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc = fuchsia_component::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let ess_store = Arc::new(KnownEssStore::new()?);
    let legacy_client = shim::ClientRef::new();
    let fidl_fut = serve_fidl(legacy_client.clone(), Arc::clone(&ess_store));

    let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
    wlan_svc.watch_devices(watcher_server_end)?;
    let listener = device::Listener::new(wlan_svc, cfg, legacy_client);
    let fut = watcher_proxy
        .take_event_stream()
        .try_for_each(|evt| device::handle_event(&listener, evt, Arc::clone(&ess_store)).map(Ok))
        .err_into()
        .and_then(|_| future::ready(Err(format_err!("Device watcher future exited unexpectedly"))));

    executor.run_singlethreaded(fidl_fut.try_join(fut)).map(|_: (Void, Void)| ())
}
