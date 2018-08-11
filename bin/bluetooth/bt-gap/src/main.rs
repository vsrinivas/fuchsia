// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fidl_fuchsia_bluetooth;
extern crate fidl_fuchsia_bluetooth_control;
extern crate fidl_fuchsia_bluetooth_gatt;
extern crate fidl_fuchsia_bluetooth_host;
extern crate fidl_fuchsia_bluetooth_le;
extern crate fuchsia_app as app;
#[macro_use]
extern crate fuchsia_async as async;
#[macro_use]
extern crate fuchsia_bluetooth as bt;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_zircon as zx;
#[macro_use]
extern crate log;
extern crate slab;

extern crate futures;
extern crate parking_lot;

use app::server::ServicesServer;
use futures::future::ok as fok;

use bt::util;
use failure::{Error, ResultExt};
use fidl::endpoints2::{ServerEnd, ServiceMarker};
use fidl_fuchsia_bluetooth_control::ControlMarker;
use fidl_fuchsia_bluetooth_gatt::Server_Marker;
use fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker};
use futures::FutureExt;
use parking_lot::RwLock;
use std::sync::Arc;

mod control_service;
mod host_device;
mod host_dispatcher;
mod logger;

use host_dispatcher::*;

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;
static LOGGER: logger::Logger = logger::Logger;

fn main() -> Result<(), Error> {
    let _ = log::set_logger(&LOGGER);
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting bt-gap...");

    let mut executor = async::Executor::new().context("Error creating executor")?;
    let hd = Arc::new(RwLock::new(HostDispatcher::new()));

    make_clones!(hd => host_hd, control_hd, central_hd, peripheral_hd, gatt_hd);

    let host_watcher = watch_hosts(host_hd);

    let server = ServicesServer::new()
        .add_service((ControlMarker::NAME, move |chan: async::Channel| {
            trace!("Spawning Control Service");
            async::spawn(control_service::make_control_service(
                control_hd.clone(),
                chan,
            ))
        }))
        .add_service((CentralMarker::NAME, move |chan: async::Channel| {
            trace!("Connecting Control Service to Adapter");
            async::spawn(HostDispatcher::get_active_adapter(central_hd.clone()).and_then(move |adapter| {
                let remote = ServerEnd::<CentralMarker>::new(chan.into());
                if let Some(adapter) = adapter {
                    let _ = adapter.read().get_host().request_low_energy_central(remote);
                }
                fok(())
            }).recover(|e| eprintln!("Failed to connect: {}", e)))
        }))
        .add_service((PeripheralMarker::NAME, move |chan: async::Channel| {
            trace!("Connecting Peripheral Service to Adapter");
            async::spawn(HostDispatcher::get_active_adapter(peripheral_hd.clone()).and_then(move |adapter| {
                let remote = ServerEnd::<PeripheralMarker>::new(chan.into());
                if let Some(adapter) = adapter {
                    let _ = adapter.read().get_host().request_low_energy_peripheral(remote);
                }
                fok(())
            }).recover(|e| eprintln!("Failed to connect: {}", e)))
        }))
        .add_service((Server_Marker::NAME, move |chan: async::Channel| {
            trace!("Connecting Gatt Service to Adapter");
            async::spawn(HostDispatcher::get_active_adapter(gatt_hd.clone()).and_then(move |adapter| {
                let remote = ServerEnd::<Server_Marker>::new(chan.into());
                if let Some(adapter) = adapter {
                    let _ = adapter.read().get_host().request_gatt_server_(remote);
                }
                fok(())
            }).recover(|e| eprintln!("Failed to connect: {}", e)))
        }))
        .start()
        .map_err(|e| e.context("error starting bt-gap service"))?;

    executor
        .run_singlethreaded(server.join(host_watcher))
        .context("failed to execute bt-gap server future")
        .map(|_| ())
        .map_err(|e| e.into())
}
