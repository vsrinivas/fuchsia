// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate clap;
extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate garnet_lib_wlan_fidl_service;
extern crate tokio_core;

use clap::{App, Arg, ArgMatches, SubCommand};
use failure::{Error, ResultExt};
use garnet_lib_wlan_fidl_service::DeviceService;
use tokio_core::reactor;

// TODO(tkilbourn): decide whether to move to yaml or macros
const CMD_PHY: &str = "phy";
const CMD_IFACE: &str = "iface";
const CMD_LIST: &str = "list";
const CMD_NEW: &str = "new";
const CMD_DEL: &str = "del";

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let matches = App::new(crate_name!())
        .version(crate_version!())
        .about(crate_description!())
        .subcommand(
            SubCommand::with_name(CMD_PHY)
                .about("commands for wlan phy devices")
                .subcommand(SubCommand::with_name(CMD_LIST).about("lists phy devices")),
        )
        .subcommand(
            SubCommand::with_name(CMD_IFACE)
                .about("commands for wlan iface devices")
                .subcommand(SubCommand::with_name(CMD_LIST).about("lists iface devices"))
                .subcommand(
                    SubCommand::with_name(CMD_NEW)
                        .about("creates a new iface device")
                        .arg(
                            Arg::with_name("phy_id")
                                .required(true)
                                .help("id of the phy that will host the iface"),
                        ),
                )
                .subcommand(
                    SubCommand::with_name(CMD_DEL)
                        .about("destroys an iface device")
                        .arg(
                            Arg::with_name("iface_id")
                                .required(true)
                                .help("iface id to destroy"),
                        ),
                ),
        )
        .get_matches();

    match matches.subcommand() {
        (CMD_PHY, Some(m)) => do_phy(m)?,
        (CMD_IFACE, _) => println!("NOT IMPLEMENTED"),
        _ => println!("{}", matches.usage()),
    }

    Ok(())
}

fn do_phy(args: &ArgMatches) -> Result<(), Error> {
    let mut core = reactor::Core::new().context("error creating event loop")?;
    let handle = core.handle();

    let wlan_dev = fuchsia_app::client::connect_to_service::<DeviceService::Service>(&handle)
        .context("failed to connect to device service")?;

    match args.subcommand() {
        (CMD_LIST, _) => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            let response_fut = wlan_dev.list_phys();
            let response = core.run(response_fut).context("error getting response")?;
            println!("response: {:?}", response);
        }
        _ => println!("{}", args.usage()),
    }
    Ok(())
}
