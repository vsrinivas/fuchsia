// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod opts;

use {
    crate::opts::*,
    failure::{Error, ResultExt},
    fidl_fuchsia_net_policy::{ObserverMarker, ObserverProxy},
    fidl_fuchsia_net_policy_ext::InterfaceInfo,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    structopt::StructOpt,
};

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let mut exec = fasync::Executor::new().context("error creating executor")?;
    let observer_proxy = fuchsia_component::client::connect_to_service::<ObserverMarker>()
        .context("Failed to connect to Observer service")?;

    let fut = async {
        match opt {
            Opt::If(cmd) => await!(process_observer(cmd, observer_proxy)),
        }
    };
    exec.run_singlethreaded(fut)?;
    Ok(())
}

async fn process_observer(cmd: ObserverCmd, observer_proxy: ObserverProxy) -> Result<(), Error> {
    match cmd {
        ObserverCmd::List => {
            let (infos, status) =
                await!(observer_proxy.list_interfaces()).context("error getting response")?;
            match zx::Status::ok(status) {
                Ok(()) => {
                    for info in infos.unwrap() {
                        println!("{}", InterfaceInfo::from(info));
                    }
                }
                Err(e) => println!("error listing interfaces:{}", e),
            }
        }
        ObserverCmd::Get { name } => {
            let (info, status) = await!(observer_proxy.get_interface_info(&name))
                .with_context(|_| format!("error getting response"))?;
            match zx::Status::ok(status) {
                Ok(()) => println!("{}", InterfaceInfo::from(*info.unwrap())),
                Err(e) => println!("error querying interface {}:{}", name, e),
            }
        }
    }
    Ok(())
}
