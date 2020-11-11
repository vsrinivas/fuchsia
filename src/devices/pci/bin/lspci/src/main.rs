// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod bridge;
mod capability;
mod config;
mod db;
mod device;
mod filter;
mod util;

// All PCI layouts and information are documented in the PCI Local Bus Specification
// https://pcisig.com/specifications/conventional/

use {
    anyhow::{Context, Error},
    argh::FromArgs,
    bridge::Bridge,
    device::Device,
    fdio, fuchsia_async,
    std::fs::File,
    std::io::prelude::*,
};

/// The PCI ID database, if available, is provided by //third_party/pciids
const PCI_DB_PATH: &str = "/boot/data/lspci/pci.ids";

#[derive(FromArgs)]
/// Display PCI information
pub struct Args {
    #[argh(positional, default = "String::from(\"/dev/sys/platform/PCI0/bus\")")]
    /// path to the fuchsia.hardware.pci service
    service: String,
    #[argh(switch, short = 'v')]
    /// print verbose device configuration
    verbose: bool,
    #[argh(switch, short = 'q')]
    /// don't print errors found trying to parse the database
    quiet: bool,
    #[argh(switch, short = 'x')]
    /// dump raw configuration space
    print_config: bool,
    #[argh(switch, short = 'n')]
    /// print numeric IDs.
    print_numeric: bool,
    #[argh(switch, short = 'N')]
    /// only print numeric IDs.
    only_print_numeric: bool,
    #[argh(option, short = 's')]
    /// [[<bus>]:][slot][.[<func>]]    Show only devices in selected slots
    filter: Option<filter::Filter>,
}

fn read_database<'a>(buf: &'a mut String) -> Result<db::PciDb<'a>, Error> {
    let mut f = File::open(PCI_DB_PATH)?;
    f.read_to_string(buf)?;
    db::PciDb::new(buf)
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let (proxy, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_pci::BusMarker>()?;

    fdio::service_connect(&args.service, server.into_channel())?;
    let mut buf = String::new();
    let db = read_database(&mut buf)
        .map_err(|e| {
            if !args.quiet {
                eprintln!("Couldn't parse PCI ID database '{}': {}", PCI_DB_PATH, e);
            }
        })
        .ok();

    // Verify the service is there before the loop so the errors to the user
    // make sense in the kpci case.
    proxy
        .get_host_bridge_info()
        .await
        .context("RPC failed. You probably want to use `k lspci` for lspci (fxbug.dev/32978)")?;

    for fidl_device in &proxy.get_devices().await? {
        let device = Device::new(fidl_device, &db, &args);
        if let Some(filter) = &args.filter {
            if !filter.matches(&device) {
                continue;
            }
        }

        if device.cfg.header_type & 0x1 == 0x1 {
            print!("{}", Bridge::new(&device));
        } else {
            print!("{}", device);
        }
    }
    Ok(())
}
