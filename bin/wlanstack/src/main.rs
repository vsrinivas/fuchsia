// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

#![feature(futures_api, arbitrary_self_types, pin)]
#![deny(warnings)]
#![deny(missing_docs)]

#[macro_use] extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_wlan_mlme as fidl_mlme;
extern crate fidl_fuchsia_wlan_device as wlan;
extern crate fidl_fuchsia_wlan_device_service as wlan_service;
extern crate fidl_fuchsia_wlan_sme as fidl_sme;
extern crate fidl_fuchsia_wlan_stats as fidl_stats;
extern crate fuchsia_app as component;
#[macro_use] extern crate fuchsia_async as async;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate fuchsia_zircon as zx;
#[macro_use] extern crate futures;
#[macro_use]
extern crate log;
extern crate parking_lot;
#[macro_use] extern crate pin_utils;
extern crate wlan_sme;

#[cfg(test)] extern crate fidl_fuchsia_wlan_tap as fidl_wlantap;
#[cfg(test)] extern crate wlantap_client;

mod device;
mod device_watch;
mod future_util;
mod logger;
mod service;
mod station;
mod stats_scheduler;
mod watchable_map;
mod watcher_service;

use component::server::ServicesServer;
use device::{PhyDevice, PhyMap, IfaceDevice, IfaceMap};
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use futures::prelude::*;
use futures::channel::mpsc::{self, UnboundedReceiver};
use std::sync::Arc;
use watchable_map::MapEvent;
use wlan_service::DeviceServiceMarker;

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

    let mut exec = async::Executor::new().context("error creating event loop")?;

    let (phys, phy_events) = device::PhyMap::new();
    let (ifaces, iface_events) = device::IfaceMap::new();
    let phys = Arc::new(phys);
    let ifaces = Arc::new(ifaces);

    let phy_server = device::serve_phys(phys.clone())?
        .and_then(|()| future::ready(Err(format_err!("Phy server exited unexpectedly"))));
    let iface_server = device::serve_ifaces(ifaces.clone())?
        .and_then(|()| future::ready(Err(format_err!("Iface server exited unexpectedly"))));
    let services_server = serve_fidl(phys, ifaces, phy_events, iface_events)?
        .map_ok(Never::into_any);

    exec.run_singlethreaded(services_server.try_join3(phy_server, iface_server))
        .map(|((), (), ())| ())
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
