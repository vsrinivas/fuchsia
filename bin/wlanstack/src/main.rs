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
extern crate fuchsia_app;
extern crate fuchsia_vfs_watcher;
extern crate fuchsia_wlan_dev as wlan_dev;
extern crate fuchsia_zircon;
extern crate futures;
extern crate garnet_lib_wlan_fidl as wlan;
extern crate garnet_lib_wlan_fidl_service as wlan_service;
#[macro_use]
extern crate log;
extern crate tokio_core;

mod logger;
mod device;
mod service;

use failure::{Error, ResultExt};
use fuchsia_app::server::ServicesServer;
use futures::Future;
use tokio_core::reactor;
use wlan_service::DeviceService;

use std::cell::RefCell;
use std::rc::Rc;

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

    let mut core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();

    let devmgr = Rc::new(RefCell::new(device::DeviceManager::new()));

    let phy_watcher = device::new_phy_watcher(PHY_PATH, devmgr.clone(), &handle)?;
    let iface_watcher = device::new_iface_watcher(IFACE_PATH, devmgr.clone(), &handle)?;

    let services_server = ServicesServer::new()
        .add_service({
            let devmgr = devmgr.clone();
            let handle = handle.clone();
            move || {
                let server = service::DeviceServiceServer::new(devmgr.clone(), &handle);
                DeviceService::Dispatcher(server)
            }
        })
        .start(&handle)
        .context("error configuring device service")?;

    core.run(services_server.join3(phy_watcher, iface_watcher))
        .map(|_| ())
}
