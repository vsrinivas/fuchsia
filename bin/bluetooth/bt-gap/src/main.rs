// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(
    futures_api,
    await_macro,
    async_await
)]

// Macros used to serialize bonding data FIDL types for persistent storage.
#[macro_use] extern crate serde_derive;

use failure::{Error, ResultExt};
use fidl::endpoints::{ServiceMarker};
use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
use fidl_fuchsia_bluetooth_control::ControlMarker;
use fidl_fuchsia_bluetooth_gatt::Server_Marker;
use fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_bluetooth::util;
use fuchsia_syslog::{self as syslog, fx_log_info};
use futures::{TryFutureExt, TryStreamExt};

mod services;
mod store;

mod adapters;
mod host_device;
mod host_dispatcher;

use crate::adapters::*;
use crate::adapters::AdapterEvent::*;
use crate::host_dispatcher::*;
use crate::host_dispatcher::HostService::*;

const BT_GAP_COMPONENT_ID: &'static str = "bt-gap";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["bt-gap"]).expect("Can't init logger");
    fx_log_info!("Starting bt-gap...");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let stash = executor.run_singlethreaded(store::stash::init_stash(BT_GAP_COMPONENT_ID))?;

    let hd = HostDispatcher::new(stash);
    let watch_hd = hd.clone();
    let central_hd = hd.clone();
    let control_hd = hd.clone();
    let peripheral_hd = hd.clone();
    let profile_hd = hd.clone();
    let gatt_hd = hd.clone();

    let host_watcher = watch_hosts().try_for_each(move |msg| {
        let hd = watch_hd.clone();
        async {
            match msg {
                AdapterAdded(device_path) => await!(hd.add_adapter(device_path)),
                AdapterRemoved(device_path) => hd.rm_adapter(device_path),
            }
        }
    });

    let server = ServicesServer::new()
        .add_service((ControlMarker::NAME, move |chan: fasync::Channel| {
            control_service(control_hd.clone(), chan)
        })).add_service((CentralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Connecting CentralService to Adapter");
            fasync::spawn(central_hd.clone().request_host_service(chan, LeCentral))
        })).add_service((PeripheralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Connecting Peripheral Service to Adapter");
            fasync::spawn(peripheral_hd.clone().request_host_service(chan, LePeripheral))
        })).add_service((ProfileMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Connecting Profile Service to Adapter");
            fasync::spawn(profile_hd.clone().request_host_service(chan, Profile))
        })).add_service((Server_Marker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Connecting Gatt Service to Adapter");
            fasync::spawn(gatt_hd.clone().request_host_service(chan, LeGatt))
        })).start()?;

    executor
        .run_singlethreaded(server.try_join(host_watcher))
        .map(|_| ())
}

fn control_service(hd: HostDispatcher, chan: fasync::Channel) {
    fx_log_info!("Spawning Control Service");
    fasync::spawn(
        services::start_control_service(hd.clone(), chan)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    )
}
