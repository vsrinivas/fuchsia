// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate clap;
extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate garnet_lib_wlan_fidl as wlan;
extern crate garnet_lib_wlan_fidl_service as wlan_service;
#[macro_use]
extern crate structopt;
extern crate tokio_core;

use failure::{Error, ResultExt};
use structopt::StructOpt;
use wlan_service::DeviceService;
use tokio_core::reactor;

mod opts;
use opts::*;

struct ExecContext {
    core: reactor::Core,
    wlan_svc: <DeviceService::Service as fidl::FidlService>::Proxy,
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();
    let cx = ExecContext {
        core: core,
        wlan_svc: fuchsia_app::client::connect_to_service::<DeviceService::Service>(&handle)
            .context("failed to connect to device service")?,
    };

    match opt {
        Opt::Phy(cmd) => do_phy(cmd, cx),
        Opt::Iface(cmd) => do_iface(cmd, cx),
    }
}

fn do_phy(cmd: opts::PhyCmd, mut cx: ExecContext) -> Result<(), Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            let response_fut = cx.wlan_svc.list_phys();
            let response = cx.core.run(response_fut).context("error getting response")?;
            println!("response: {:?}", response);
            Ok(())
        }
    }
}

fn do_iface(cmd: opts::IfaceCmd, mut cx: ExecContext) -> Result<(), Error> {
    match cmd {
        opts::IfaceCmd::New { phy_id, role } => {
            let req = wlan_service::CreateIfaceRequest {
                phy_id: phy_id,
                role: role.into(),
            };
            let response_fut = cx.wlan_svc.create_iface(req);
            let response = cx.core.run(response_fut).context("error getting response")?;
            println!("response: {:?}", response);
            Ok(())
        }
        opts::IfaceCmd::Delete { phy_id, iface_id } => {
            let req = wlan_service::DestroyIfaceRequest {
                phy_id: phy_id,
                iface_id: iface_id,
            };
            cx.wlan_svc.destroy_iface(req)?;
            println!("deleted iface {:?}", iface_id);
            Ok(())
        }
    }
}
