// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All PCI layouts and information are documented in the PCI Local Bus Specification
// https://pcisig.com/specifications/conventional/
use {
    anyhow::{anyhow, Error},
    fdio,
    fidl_fuchsia_hardware_pci::HeaderType,
    files_async::{dir_contains, readdir, DirentKind},
    fuchsia_async,
    io_util::{directory::open_in_namespace, OpenFlags},
    lspci::{bridge::Bridge, db, device::Device, Args},
    std::fs::File,
    std::io::prelude::*,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();

    // Find PCI buses and connect to them.
    let proxies = find_buses(&args.service).await?;

    // Read the database.
    let mut buf = String::new();
    let db = read_database(&mut buf)
        .map_err(|e| {
            if !args.quiet {
                eprintln!("Couldn't parse PCI ID database '{}': {}", PCI_DB_PATH, e);
            }
        })
        .ok();

    for proxy in proxies {
        for fidl_device in &proxy.get_devices().await? {
            let device = Device::new(fidl_device, &db, &args);
            if let Some(filter) = &args.filter {
                if !filter.matches(&device) {
                    continue;
                }
            }

            if device.cfg.header_type & HeaderType::Bridge.into_primitive() > 0 {
                print!("{}", Bridge::new(&device));
            } else {
                print!("{}", device);
            }
        }
    }
    Ok(())
}

/// The PCI ID database, if available, is provided by //third_party/pciids
const PCI_DB_PATH: &str = "/boot/data/lspci/pci.ids";

fn read_database<'a>(buf: &'a mut String) -> Result<db::PciDb<'a>, Error> {
    let mut f = File::open(PCI_DB_PATH)?;
    f.read_to_string(buf)?;
    db::PciDb::new(buf)
}

// Find 'bus' entries that correspond to fuchsia.hardware.pci services.
async fn find_buses(path: &str) -> Result<Vec<fidl_fuchsia_hardware_pci::BusProxy>, Error> {
    let mut proxies = Vec::new();
    for dir in readdir(&open_in_namespace(path, OpenFlags::RIGHT_READABLE)?)
        .await?
        .into_iter()
        .filter(|entry| entry.kind == DirentKind::Directory)
    {
        let dir_name = format!("{}/{}", path, dir.name);
        let bus_name = format!("{}/bus", dir_name);
        let dir_proxy = open_in_namespace(&dir_name, OpenFlags::RIGHT_READABLE)?;
        if dir_contains(&dir_proxy, "bus").await? {
            let (proxy, server) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_hardware_pci::BusMarker>()?;
            match fdio::service_connect(&bus_name, server.into_channel()) {
                Ok(_) => proxies.push(proxy),
                Err(status) => {
                    eprintln!("Couldn't connect to PCI bus service at '{}': {}", bus_name, status)
                }
            }
        }
    }

    if proxies.is_empty() {
        // Although it looks strange here, it lines up properly with anyhow!'s Error: prefix.
        Err(anyhow!(format!(
            "Couldn't find a PCI bus service in {}
       You may be able to manually specify the platform directory manually.
       Otherwise, due to fxbug.dev/32978 you may have success using `k lspci` instead of `lspci`.",
            path
        )))
    } else {
        Ok(proxies)
    }
}
