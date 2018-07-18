// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_wlan_mlme as fidl_mlme;
extern crate fidl_fuchsia_wlan_service as legacy;
extern crate fidl_fuchsia_wlan_sme as fidl_sme;
extern crate fidl_fuchsia_wlan_stats as fidl_wlan_stats;
extern crate fidl_fuchsia_wlan_device as wlan;
extern crate fidl_fuchsia_wlan_device_service as wlan_service;
extern crate fuchsia_app as app;
#[macro_use]
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate log;
extern crate parking_lot;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;

#[cfg(test)]
extern crate tempdir;

mod config;
mod device;
mod ess_store;
mod shim;
mod state_machine;

use config::Config;
use app::server::ServicesServer;
use failure::{Error, ResultExt};
use futures::prelude::*;
use wlan_service::DeviceServiceMarker;

fn serve_fidl(_client_ref: shim::ClientRef)
    -> impl Future<Item = Never, Error = Error>
{
    ServicesServer::new()
        // To test the legacy API server, change
        //     "fuchsia.wlan.service.Wlan": "wlanstack"
        // to
        //     "fuchsia.wlan.service.Wlan": "wlancfg"
        // in 'bin/sysmgr/config/services.config' and uncomment the following code:
        /*
        .add_service((<legacy::WlanMarker as ::fidl::endpoints2::ServiceMarker>::NAME, move |channel| {
            let stream = <legacy::WlanRequestStream as ::fidl::endpoints2::RequestStream>::from_channel(channel);
            let fut = shim::serve_legacy(stream, _client_ref.clone())
                .recover(|e| eprintln!("error serving legacy wlan API: {}", e));
            async::spawn(fut)
        }))
        */
        .start()
        .into_future()
        .and_then(|fut| fut)
        .and_then(|()| Err(format_err!("FIDL server future exited unexpectedly")))
}

fn main() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = async::Executor::new().context("error creating event loop")?;
    let wlan_svc = app::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let legacy_client = shim::ClientRef::new();
    let fidl_fut = serve_fidl(legacy_client.clone());

    let (watcher_proxy, watcher_server_end) = fidl::endpoints2::create_endpoints()?;
    wlan_svc.watch_devices(watcher_server_end)?;
    let listener = device::Listener::new(wlan_svc, cfg, legacy_client);
    let fut = watcher_proxy.take_event_stream()
        .for_each(move |evt| device::handle_event(&listener, evt))
        .err_into()
        .and_then(|_| Err(format_err!("Device watcher future exited unexpectedly")));

    executor
        .run_singlethreaded(fidl_fut.join(fut))
        .map(|_: (Never, Never)| ())
}
