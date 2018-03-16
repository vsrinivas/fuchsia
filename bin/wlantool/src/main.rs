// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(conservative_impl_trait)]

#[macro_use]
extern crate clap;
extern crate failure;
extern crate fidl;
extern crate fuchsia_async as async;
extern crate fuchsia_app as component;
extern crate futures;
extern crate garnet_lib_wlan_fidl as wlan;
extern crate garnet_lib_wlan_fidl_service as wlan_service;
#[macro_use]
extern crate structopt;

use component::client::connect_to_service;
use failure::{Error, Fail, ResultExt};
use futures::prelude::*;
use structopt::StructOpt;
use wlan_service::DeviceService;

mod opts;
use opts::*;

type WlanSvc = <DeviceService::Service as fidl::FidlService>::Proxy;

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = async::Executor::new().context("error creating event loop")?;
    let wlan_svc = connect_to_service::<DeviceService::Service>()
                    .context("failed to connect to device service")?;


    let fut = match opt {
        Opt::Phy(cmd) => do_phy(cmd, wlan_svc).left(),
        Opt::Iface(cmd) => do_iface(cmd, wlan_svc).right(),
    };

    exec.run_singlethreaded(fut)
}

fn do_phy(cmd: opts::PhyCmd, wlan_svc: WlanSvc)
    -> impl Future<Item = (), Error = Error>
{
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            wlan_svc.list_phys()
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                })
        }
    }
}

fn do_iface(cmd: opts::IfaceCmd, wlan_svc: WlanSvc)
    -> impl Future<Item = (), Error = Error>
{
    match cmd {
        opts::IfaceCmd::New { phy_id, role } => {
            let req = wlan_service::CreateIfaceRequest {
                phy_id: phy_id,
                role: role.into(),
            };

            wlan_svc.create_iface(req)
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                })
                .left()
        }
        opts::IfaceCmd::Delete { phy_id, iface_id } => {
            let req = wlan_service::DestroyIfaceRequest {
                phy_id: phy_id,
                iface_id: iface_id,
            };

            wlan_svc.destroy_iface(req)
                .map(|()| println!("deleted iface {:?}", iface_id))
                .map_err(|e| e.context("error destroying iface").into())
                .into_future()
                .right()
        }
    }
}
