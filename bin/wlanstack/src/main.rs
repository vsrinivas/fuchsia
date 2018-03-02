// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

// These features both have stable implementations and will become available on stable compilers
// soon. They allow return types of `impl Future` rather than boxing or otherwise having to name
// the future types.
#![feature(conservative_impl_trait, universal_impl_trait)]
#![deny(warnings)]
#![deny(missing_docs)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_lib_wlan_fidl as wlan;
extern crate garnet_lib_wlan_fidl_service as wlan_service;
#[macro_use]
extern crate log;
extern crate parking_lot;

mod logger;
mod device;
mod service;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;
use wlan_service::DeviceService;


const MAX_LOG_LEVEL: log::LogLevelFilter = log::LogLevelFilter::Info;

const PHY_PATH: &str = "/dev/class/wlanphy";
const IFACE_PATH: &str = "/dev/class/wlanif";

fn main() {
    if let Err(e) = main_res() {
        error!("Error: {:?}", e);
    }
    info!("Exiting");
}

fn main_res() -> Result<(), Error> {
    log::set_logger(|max_level| {
        max_level.set(MAX_LOG_LEVEL);
        Box::new(logger::Logger)
    })?;
    info!("Starting");

    let mut exec = async::Executor::new().context("error creating event loop")?;

    let devmgr = Arc::new(Mutex::new(device::DeviceManager::new()));

    let phy_watcher = device::new_phy_watcher(PHY_PATH, devmgr.clone());
    let iface_watcher = device::new_iface_watcher(IFACE_PATH, devmgr.clone());

    let services_server = ServicesServer::new()
        .add_service({
            let devmgr = devmgr.clone();
            move || DeviceService::Dispatcher(service::device_service(devmgr.clone()))
        })
        .start()
        .context("error configuring device service")?;

    exec.run_singlethreaded(services_server.join3(phy_watcher, iface_watcher))
        .map(|_| ())
}
