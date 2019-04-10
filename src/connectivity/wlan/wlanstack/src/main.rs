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

use failure::Error;
use fidl_fuchsia_inspect::InspectRequestStream;
use fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream;
use fuchsia_async as fasync;
use fuchsia_cobalt;
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect as finspect;
use futures::prelude::*;
use log::info;
use std::sync::Arc;
use wlan_inspect;

use crate::device::{IfaceDevice, IfaceMap, PhyDevice, PhyMap};
use crate::watcher_service::WatcherService;

const COBALT_CONFIG_PATH: &'static str = "/pkg/data/wlan_metrics_registry.pb";
const COBALT_BUFFER_SIZE: usize = 100;
const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

const CONCURRENT_LIMIT: usize = 1000;

static LOGGER: logger::Logger = logger::Logger;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");

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
    let (watcher_service, watcher_fut) =
        watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);
    let services_server =
        serve_fidl(phys, ifaces, watcher_service, inspect_root).try_join(watcher_fut);

    await!(services_server.try_join5(
        phy_server,
        iface_server,
        cobalt_reporter.map(Ok),
        telemetry_server.map(Ok),
    ))?;
    Ok(())
}

enum IncomingServices {
    Device(DeviceServiceRequestStream),
    Inspect(InspectRequestStream),
}

async fn serve_fidl(
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: WatcherService<PhyDevice, IfaceDevice>,
    inspect_root: wlan_inspect::SharedNodePtr,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("public")
        .add_fidl_service(IncomingServices::Device)
        .add_fidl_service(IncomingServices::Inspect);

    fs.take_and_serve_directory_handle()?;

    let fdio_server = fs.for_each_concurrent(CONCURRENT_LIMIT, move |s| {
        let phys = phys.clone();
        let ifaces = ifaces.clone();
        let watcher_service = watcher_service.clone();
        let inspect_root = inspect_root.clone();
        async move {
            match s {
                IncomingServices::Device(stream) => {
                    await!(service::serve_device_requests(phys, ifaces, watcher_service, stream)
                        .unwrap_or_else(|e| println!("{:?}", e)))
                }
                IncomingServices::Inspect(stream) => {
                    await!(finspect::serve_request_stream(inspect_root, stream)
                        .unwrap_or_else(|e| println!("{:?}", e)))
                }
            }
        }
    });
    await!(fdio_server);
    Ok(())
}
