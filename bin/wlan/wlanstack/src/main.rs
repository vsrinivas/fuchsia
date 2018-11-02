// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

#![feature(async_await, await_macro, futures_api, arbitrary_self_types, pin)]
#![deny(warnings)]
#![deny(missing_docs)]

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

use failure::{Error, format_err, ResultExt};
use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_wlan_device_service::DeviceServiceMarker;
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_cobalt;
use futures::prelude::*;
use futures::channel::mpsc::{self, UnboundedReceiver};
use log::info;
use std::sync::Arc;

use crate::device::{PhyDevice, PhyMap, IfaceDevice, IfaceMap};
use crate::watchable_map::MapEvent;

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

static LOGGER: logger::Logger = logger::Logger;

#[allow(missing_docs)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Never {}

impl Never {
    #[allow(missing_docs)]
    pub fn into_any<T>(self) -> T { match self {} }
}

fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");

    let mut exec = fasync::Executor::new().context("error creating event loop")?;

    let (phys, phy_events) = device::PhyMap::new();
    let (ifaces, iface_events) = device::IfaceMap::new();
    let phys = Arc::new(phys);
    let ifaces = Arc::new(ifaces);

    let phy_server = device::serve_phys(phys.clone())
        .map_ok(|x| x.into_any());
    let (cobalt_sender, cobalt_reporter) = fuchsia_cobalt::serve();
    let telemetry_server = telemetry::report_telemetry_periodically(ifaces.clone(), cobalt_sender.clone());
    let iface_server = device::serve_ifaces(ifaces.clone(), cobalt_sender)
        .map_ok(|x| x.into_any());
    let services_server = serve_fidl(phys, ifaces, phy_events, iface_events)?
        .map_ok(Never::into_any);

    exec.run_singlethreaded(services_server.try_join5(phy_server,
                                                      iface_server,
                                                      cobalt_reporter.map(Ok),
                                                      telemetry_server.map(Ok)))
        .map(|((), (), (), (), ())| ())

}

fn serve_fidl(phys: Arc<PhyMap>, ifaces: Arc<IfaceMap>,
              phy_events: UnboundedReceiver<MapEvent<u16, PhyDevice>>,
              iface_events: UnboundedReceiver<MapEvent<u16, IfaceDevice>>)
    -> Result<impl Future<Output = Result<Never, Error>>, Error>
{
    let (sender, receiver) = mpsc::unbounded();
    let fdio_server = ServicesServer::new()
        .add_service((DeviceServiceMarker::NAME, move |channel| {
            sender.unbounded_send(channel).expect("Failed to send a new client to the server future");
        }))
        .start()
        .context("error configuring device service")?
        .and_then(|()| future::ready(Err(format_err!("fdio server future exited unexpectedly"))))
        .map_err(|e| e.context("fdio server terminated with error").into());
    let device_service = service::device_service(phys, ifaces, phy_events, iface_events, receiver);
    Ok(fdio_server.try_join(device_service)
        .map_ok(|x: (Never, Never)| x.0))
}
