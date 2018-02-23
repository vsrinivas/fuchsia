// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_lib_wlan_fidl as wlan;
extern crate garnet_lib_wlan_fidl_service as wlan_service;
extern crate tokio_core;
extern crate tokio_fuchsia;

use failure::{Error, ResultExt};
use futures::Future;
use tokio_core::reactor;
use wlan_service::{DeviceListener, DeviceService};

struct Listener(<DeviceService::Service as fidl::FidlService>::Proxy);

impl DeviceListener::Server for Listener {
    type OnPhyAdded = fidl::BoxServerFuture<()>;
    fn on_phy_added(&mut self, id: u16) -> Self::OnPhyAdded {
        println!("wlancfg: phy added: {}", id);
        // For now, just create a Client iface on the new phy.
        // TODO(tkilbourn): get info about this phy, then consult a configuration file to determine
        // what interfaces to create.
        let req = wlan_service::CreateIfaceRequest {
            phy_id: id,
            role: wlan::MacRole::Client,
        };
        Box::new(
            self.0
                .create_iface(req)
                .map(|_| ())
                .map_err(|_| fidl::CloseChannel),
        )
    }

    type OnPhyRemoved = fidl::ServerImmediate<()>;
    fn on_phy_removed(&mut self, id: u16) -> Self::OnPhyRemoved {
        println!("wlancfg: phy removed: {}", id);
        futures::future::ok(())
    }

    type OnIfaceAdded = fidl::ServerImmediate<()>;
    fn on_iface_added(&mut self, phy_id: u16, iface_id: u16) -> Self::OnIfaceAdded {
        println!("wlancfg: iface added: {} (phy={})", iface_id, phy_id);
        futures::future::ok(())
    }

    type OnIfaceRemoved = fidl::ServerImmediate<()>;
    fn on_iface_removed(&mut self, phy_id: u16, iface_id: u16) -> Self::OnIfaceRemoved {
        println!("wlancfg: iface removed: {} (phy={}", iface_id, phy_id);
        futures::future::ok(())
    }
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
    println!("wlancfg: Exiting");
}

fn main_res() -> Result<(), Error> {
    let mut core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();
    let wlan_svc = fuchsia_app::client::connect_to_service::<DeviceService::Service>(&handle)
        .context("failed to connect to device service")?;

    let (remote, local) = zx::Channel::create().context("failed to create zx channel")?;
    let remote_ptr = fidl::InterfacePtr {
        inner: fidl::ClientEnd::new(remote),
        version: DeviceListener::VERSION,
    };
    wlan_svc
        .register_listener(remote_ptr)
        .context("failed to register listener")?;

    let listener = Listener(wlan_svc);
    let listener_fut = fidl::Server::new(DeviceListener::Dispatcher(listener), local, &handle)
        .context("failed to create listener server")?;

    core.run(listener_fut).map_err(|e| e.into())
}
