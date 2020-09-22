// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::ControlMarker,
    fidl_fuchsia_bluetooth_gatt::Server_Marker,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_bluetooth_snoop::SnoopMarker,
    fidl_fuchsia_bluetooth_sys::{
        AccessMarker, BootstrapMarker, ConfigurationMarker, HostWatcherMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::{client, fuchsia_single_component_package_url, server},
    futures::{
        future::{self, try_join},
        FutureExt, StreamExt,
    },
    log::{info, warn},
};

mod config;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-init"]).expect("Can't init logger");
    info!("Starting bt-init...");

    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let cfg = config::Config::load()?;

    // Start bt-snoop service before anything else and hold onto the connection until bt-init exits.
    let snoop_connection;
    if cfg.autostart_snoop() {
        snoop_connection = client::connect_to_service::<SnoopMarker>();
        if let Err(e) = snoop_connection {
            warn!("Failed to start snoop service: {}", e);
        }
    }

    let launcher = client::launcher().expect("Failed to launch bt-gap (bluetooth) service");
    let bt_gap = client::launch(
        &launcher,
        fuchsia_single_component_package_url!("bt-gap").to_string(),
        None,
    )?;

    let mut fs = server::ServiceFs::new();
    fs.dir("svc")
        .add_service_at(AccessMarker::NAME, |chan| Some((AccessMarker::NAME, chan)))
        .add_service_at(BootstrapMarker::NAME, |chan| Some((BootstrapMarker::NAME, chan)))
        .add_service_at(ConfigurationMarker::NAME, |chan| Some((ConfigurationMarker::NAME, chan)))
        .add_service_at(ControlMarker::NAME, |chan| Some((ControlMarker::NAME, chan)))
        .add_service_at(CentralMarker::NAME, |chan| Some((CentralMarker::NAME, chan)))
        .add_service_at(HostWatcherMarker::NAME, |chan| Some((HostWatcherMarker::NAME, chan)))
        .add_service_at(PeripheralMarker::NAME, |chan| Some((PeripheralMarker::NAME, chan)))
        .add_service_at(ProfileMarker::NAME, |chan| Some((ProfileMarker::NAME, chan)))
        .add_service_at(Server_Marker::NAME, |chan| Some((Server_Marker::NAME, chan)));
    fs.take_and_serve_directory_handle()?;
    let server = fs
        .for_each(move |(name, chan)| {
            info!("Passing {} Handle to bt-gap", name);
            let _ = bt_gap.pass_to_named_service(name, chan);
            future::ready(())
        })
        .map(Ok);

    let io_config_fut = cfg.set_capabilities();

    executor
        .run_singlethreaded(try_join(server, io_config_fut))
        .context("bt-init failed to execute future")
        .map_err(|e| e.into())
        .map(|_| ())
}
