// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

// These features both have stable implementations and will become available on stable compilers
// soon. They allow return types of `impl Future` rather than boxing or otherwise having to name
// the future types.
#![deny(warnings)]
#![deny(missing_docs)]

extern crate failure;
extern crate fidl;
extern crate fidl_wlan_mlme as fidl_mlme;
extern crate fidl_wlan_device as wlan;
extern crate fidl_wlan_device_service as wlan_service;
extern crate fidl_wlan_service;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate log;
extern crate parking_lot;
extern crate wlan_sme;

mod device;
mod logger;
mod service;
mod station;

use component::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;
use wlan_service::DeviceServiceMarker;

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

const PHY_PATH: &str = "/dev/class/wlanphy";
const IFACE_PATH: &str = "/dev/class/wlanif";

static LOGGER: logger::Logger = logger::Logger;

fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");

    let mut exec = async::Executor::new().context("error creating event loop")?;

    let devmgr = Arc::new(Mutex::new(device::DeviceManager::new()));

    let phy_watcher = device::new_phy_watcher(PHY_PATH, devmgr.clone());
    let iface_watcher = device::new_iface_watcher(IFACE_PATH, devmgr.clone());

    let services_server = ServicesServer::new()
        .add_service((DeviceServiceMarker::NAME, move |channel| {
            async::spawn(service::device_service(devmgr.clone(), channel))
        }))
        .start()
        .context("error configuring device service")?;

    exec.run_singlethreaded(services_server.join3(phy_watcher, iface_watcher))
        .map(|_| ())
}
