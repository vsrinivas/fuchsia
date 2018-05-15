// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]
#![feature(use_extern_macros, conservative_impl_trait, universal_impl_trait)]

extern crate failure;
extern crate fdio;
extern crate fidl;
extern crate fidl_bluetooth;
extern crate fidl_bluetooth_control;
extern crate fidl_bluetooth_gatt;
extern crate fidl_bluetooth_host;
extern crate fidl_bluetooth_low_energy;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
#[macro_use]
extern crate fuchsia_bluetooth as bt;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_zircon as zx;
#[macro_use]
extern crate log;

extern crate futures;
extern crate parking_lot;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::{ServerEnd, ServiceMarker};
use fidl_bluetooth_control::ControlMarker;
use fidl_bluetooth_gatt::Server_Marker;
use fidl_bluetooth_low_energy::{CentralMarker, PeripheralMarker};
use futures::FutureExt;
use parking_lot::RwLock;
use std::sync::Arc;

use bt::util;
mod adapter;
mod control_service;
mod host_dispatcher;
mod logger;

use host_dispatcher::*;

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;
static LOGGER: logger::Logger = logger::Logger;

fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER);
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
            if let Some(adap) = central_hd.write().get_active_adapter() {
                let remote = ServerEnd::<CentralMarker>::new(chan.into());
                adap.lock()
                    .get_host()
                    .request_low_energy_central(remote);
            }
        }))
        .add_service((PeripheralMarker::NAME, move |chan: async::Channel| {
            trace!("Connecting Peripheral Service to Adapter");
            if let Some(adap) = peripheral_hd.write().get_active_adapter() {
                let mut remote = ServerEnd::<PeripheralMarker>::new(chan.into());
                adap.lock()
                    .get_host()
                    .request_low_energy_peripheral(remote);
            }
        }))
        .add_service((Server_Marker::NAME, move |chan: async::Channel| {
            trace!("Connecting Gatt Service to Adapter");
            if let Some(adap) = gatt_hd.write().get_active_adapter() {
                let remote = ServerEnd::<Server_Marker>::new(chan.into());
                adap.lock().get_host().request_gatt_server_(remote);
            }
        }))
        .start()
        .map_err(|e| e.context("error starting bt-gap service"))?;

    executor
        .run_singlethreaded(server.join(host_watcher))
        .context("failed to execute bt-gap server future")
        .map(|_| ())
        .map_err(|e| e.into())
}
