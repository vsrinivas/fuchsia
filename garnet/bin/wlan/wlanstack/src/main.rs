// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]
#![deny(missing_docs)]
#![recursion_limit = "256"]

mod device;
mod device_watch;
mod fidl_util;
mod future_util;
mod logger;
mod mlme_query_proxy;
mod service;
mod station;
mod stats_scheduler;
mod telemetry;
mod watchable_map;
mod watcher_service;

use failure::{format_err, Error, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_inspect as fidl_inspect;
use fidl_fuchsia_wlan_device_service::DeviceServiceMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_cobalt;
use fuchsia_inspect as finspect;
use futures::channel::mpsc::{self, UnboundedReceiver};
use futures::prelude::*;
use log::info;
use std::sync::Arc;
use void::Void;
use wlan_inspect;

use crate::device::{IfaceDevice, IfaceMap, PhyDevice, PhyMap};
use crate::watchable_map::MapEvent;

const COBALT_CONFIG_PATH: &'static str = "/pkg/data/wlan_metrics_registry.pb";
const COBALT_BUFFER_SIZE: usize = 100;
const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

static LOGGER: logger::Logger = logger::Logger;

fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");

    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let inspect_root = finspect::ObjectTreeNode::new_root();
    let (phys, phy_events) = device::PhyMap::new();
    let (ifaces, iface_events) = device::IfaceMap::new();
    let phys = Arc::new(phys);
    let ifaces = Arc::new(ifaces);

    let phy_server = device::serve_phys(phys.clone()).map_ok(|x| match x {});
    let (cobalt_sender, cobalt_reporter) =
        fuchsia_cobalt::serve(COBALT_BUFFER_SIZE, COBALT_CONFIG_PATH);
    let telemetry_server =
        telemetry::report_telemetry_periodically(ifaces.clone(), cobalt_sender.clone());
    let iface_server = device::serve_ifaces(ifaces.clone(), cobalt_sender, inspect_root.clone())
        .map_ok(|x| match x {});
    let services_server = serve_fidl(phys, ifaces, phy_events, iface_events, inspect_root)?
        .map_ok(|void| match void {});

    exec.run_singlethreaded(services_server.try_join5(
        phy_server,
        iface_server,
        cobalt_reporter.map(Ok),
        telemetry_server.map(Ok),
    ))
    .map(|((), (), (), (), ())| ())
}

fn serve_fidl(
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    phy_events: UnboundedReceiver<MapEvent<u16, PhyDevice>>,
    iface_events: UnboundedReceiver<MapEvent<u16, IfaceDevice>>,
    inspect_root: wlan_inspect::SharedNodePtr,
) -> Result<impl Future<Output = Result<Void, Error>>, Error> {
    let (sender, receiver) = mpsc::unbounded();
    let fdio_server = ServicesServer::new()
        .add_service((DeviceServiceMarker::NAME, move |channel| {
            sender
                .unbounded_send(channel)
                .expect("Failed to send a new client to the server future");
        }))
        .add_service((fidl_inspect::InspectMarker::NAME, move |channel| {
            finspect::InspectService::new(inspect_root.clone(), channel)
        }))
        .start()
        .context("error configuring device service")?
        .and_then(|()| future::ready(Err(format_err!("fdio server future exited unexpectedly"))))
        .map_err(|e| e.context("fdio server terminated with error").into());
    let device_service = service::device_service(phys, ifaces, phy_events, iface_events, receiver);
    Ok(fdio_server.try_join(device_service).map_ok(|x: (Void, Void)| x.0))
}
