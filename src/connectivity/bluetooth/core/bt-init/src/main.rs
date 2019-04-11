// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::DiscoverableService,
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::ControlMarker,
    fidl_fuchsia_bluetooth_gatt::Server_Marker,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_bluetooth_snoop::SnoopMarker,
    fuchsia_app::{
        client::{connect_to_service, Launcher},
        server::ServicesServer,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::make_clones,
    fuchsia_syslog::{self as syslog, fx_log_info, fx_log_warn},
    futures::TryFutureExt,
    parking_lot::Mutex,
    std::sync::Arc,
};

mod config;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["bt-init"]).expect("Can't init logger");
    fx_log_info!("Starting bt-init...");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let cfg = config::Config::load()?;

    // Start bt-snoop service before anything else.
    if cfg.autostart_snoop() {
        let res = connect_to_service::<SnoopMarker>();
        if let Err(e) = res {
            fx_log_warn!("Failed to start snoop service: {}", e);
        }
    }

    let launcher = Launcher::new().context("Failed to open launcher service").unwrap();
    let btgap = Arc::new(Mutex::new(
        launcher
            .launch(String::from("fuchsia-pkg://fuchsia.com/bt-gap#meta/bt-gap.cmx"), None)
            .context("Failed to launch bt-gap (bluetooth) service")
            .unwrap(),
    ));

    make_clones!(btgap => btgap_control, btgap_central, btgap_peripheral, btgap_profile, btgap_gatt_server);
    let server = ServicesServer::new()
        .add_service((ControlMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Control Handle to bt-gap");
            let _ = btgap_control.lock().pass_to_service(ControlMarker, chan.into());
        }))
        .add_service((CentralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing LE Central Handle to bt-gap");
            let _ = btgap_central.lock().pass_to_service(CentralMarker, chan.into());
        }))
        .add_service((PeripheralMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Peripheral Handle to bt-gap");
            let _ = btgap_peripheral.lock().pass_to_service(PeripheralMarker, chan.into());
        }))
        .add_service((ProfileMarker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing Profile Handle to bt-gap");
            let _ = btgap_profile.lock().pass_to_service(ProfileMarker, chan.into());
        }))
        .add_service((Server_Marker::NAME, move |chan: fasync::Channel| {
            fx_log_info!("Passing GATT Handle to bt-gap");
            let _ = btgap_gatt_server.lock().pass_to_service(Server_Marker, chan.into());
        }))
        .start()
        .map_err(|e| e.context("error starting service server"))?;

    let io_config_fut = cfg.set_capabilities();
    executor
        .run_singlethreaded(server.try_join(io_config_fut))
        .context("bt-init failed to execute future")
        .map_err(|e| e.into())
        .map(|_| ())
}
