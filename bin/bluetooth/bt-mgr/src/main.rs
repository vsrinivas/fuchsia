// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types)]

#[macro_use] extern crate serde_derive;

use fuchsia_bluetooth::make_clones;
use fuchsia_async as fasync;
use fuchsia_app::{server::ServicesServer, client::Launcher};
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use fidl_fuchsia_bluetooth_control::BondingMarker;
use fidl_fuchsia_bluetooth_control::ControlMarker;
use fidl_fuchsia_bluetooth_gatt::Server_Marker;
use fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker};
use futures::{TryStreamExt, TryFutureExt};
use parking_lot::{Mutex, RwLock};
use fuchsia_syslog::macros::*;
use std::sync::Arc;
use crate::bond_store::BondStore;

mod bond_defs;
mod bond_store;
mod config;

fn main() -> Result<(), Error> {
    eprintln!("Starting bt-mgr...");
    let mut executor = fasync::Executor::new().context("Error creating executor")?;

    let launcher = Launcher::new()
        .context("Failed to open launcher service")
        .unwrap();
    let btgap = Arc::new(Mutex::new(
        launcher
            .launch(String::from("bt-gap"), None)
            .context("Failed to launch bt-gap (bluetooth) service")
            .unwrap(),
    ));

    make_clones!(btgap => btgap_control, btgap_central, btgap_peripheral, btgap_server);

    let app = btgap.lock().connect_to_service(BondingMarker).unwrap();
    let bond_store = Arc::new(RwLock::new(BondStore::load_store()?));
    let bs = bond_store.clone();
    let bond_app = app.clone();

    let bond_fut = async {
        await!(config::set_capabilities())?;
        await!(bond_store::restore_bonded_devices(bs.clone(), bond_app.clone()))?;
        let mut stream = app.take_event_stream();
        while let Some(evt) = await!(stream.try_next())? {
            bond_store::bond_event(bond_store.clone(), evt)?
        }
        Ok(())
    };

    let server = ServicesServer::new()
        .add_service((ControlMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Control Handle to bt-gap");
            let _ = btgap_control
                .lock()
                .pass_to_service(ControlMarker, chan.into());
        }))
        .add_service((CentralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing LE Central Handle to bt-gap");
            let _ = btgap_central
                .lock()
                .pass_to_service(CentralMarker, chan.into());
        }))
        .add_service((PeripheralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Peripheral Handle to bt-gap");
            let _ = btgap_peripheral
                .lock()
                .pass_to_service(PeripheralMarker, chan.into());
        }))
        .add_service((Server_Marker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing GATT Handle to bt-gap");
            let _ = btgap_server
                .lock()
                .pass_to_service(Server_Marker, chan.into());
        }))
        .start()
        .map_err(|e| e.context("error starting service server"))?;

        executor
            .run_singlethreaded(server.try_join(bond_fut))
            .context("bt-mgr failed to execute future")
            .map_err(|e| e.into())
            .map(|_| ())
}
