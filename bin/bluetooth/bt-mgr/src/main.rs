// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use fuchsia_bluetooth::make_clones;
use fuchsia_async as fasync;
use fuchsia_app::{server::ServicesServer, client::Launcher};
use failure::{Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_bluetooth_bredr::ProfileMarker;
use fidl_fuchsia_bluetooth_control::ControlMarker;
use fidl_fuchsia_bluetooth_gatt::Server_Marker;
use fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker};
use futures::TryFutureExt;
use parking_lot::Mutex;
use fuchsia_syslog::{self as syslog, fx_log_info};
use std::sync::Arc;

mod config;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["bt-mgr"]).expect("Can't init logger");
    fx_log_info!("Starting bt-mgr...");
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

    make_clones!(btgap => btgap_control, btgap_central, btgap_peripheral, btgap_profile, btgap_gatt_server);
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
        .add_service((ProfileMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Profile Handle to bt-gap");
            let _ = btgap_profile
                .lock()
                .pass_to_service(ProfileMarker, chan.into());
        }))
        .add_service((Server_Marker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing GATT Handle to bt-gap");
            let _ = btgap_gatt_server
                .lock()
                .pass_to_service(Server_Marker, chan.into());
        }))
        .start()
        .map_err(|e| e.context("error starting service server"))?;

        let io_config_fut = config::set_capabilities();
        executor
            .run_singlethreaded(server.try_join(io_config_fut))
            .context("bt-mgr failed to execute future")
            .map_err(|e| e.into())
            .map(|_| ())
}
