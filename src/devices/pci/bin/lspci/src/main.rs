// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// All PCI layouts and information are documented in the PCI Local Bus Specification
// https://pcisig.com/specifications/conventional/
use {
    anyhow::{anyhow, Context, Error},
    fdio,
    fidl_fuchsia_hardware_pci::{Address, BusMarker, BusProxy, HeaderType},
    fuchsia_async,
    fuchsia_fs::directory::{dir_contains, readdir, DirentKind},
    fuchsia_fs::{directory::open_in_namespace, OpenFlags},
    fuchsia_zircon::Status,
    lspci::{
        bridge::Bridge, db, device::Device, filter::Filter, util::Hexdumper, Args, SubCommand,
    },
    std::fs::File,
    std::io::prelude::*,
};

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();
    let proxies = find_buses(&args.service).await?;
    handle_subcommands(&proxies, args).await
}

async fn handle_subcommands<'a>(proxies: &'a Vec<BusProxy>, args: Args) -> Result<(), Error> {
    // Read the database.
    let mut buf = String::new();
    let db = read_database(&mut buf)
        .map_err(|e| {
            if !args.quiet {
                eprintln!("Couldn't parse PCI ID database '{}': {}", PCI_DB_PATH, e);
            }
        })
        .ok();

    match args.command {
        Some(SubCommand::Buses(..)) => {
            for proxy in proxies {
                let info = proxy.get_host_bridge_info().await?;
                println!(
                    "{}: segment {:04x}, bus start {:#04x}, bus end {:#04x}",
                    info.name, info.segment_group, info.start_bus_number, info.end_bus_number,
                );
            }
        }
        Some(SubCommand::Read(options)) => {
            let bus = find_bus_containing_bdf(&options.device, &proxies).await?;
            if options.device.dev.is_none() || options.device.func.is_none() {
                return Err(anyhow!(
                    "Invalid device {}, read requires a full device BDF.",
                    options.device
                ));
            }

            let mut bdf = Address {
                bus: options.device.bus,
                device: options.device.dev.unwrap(),
                function: options.device.func.unwrap(),
            };

            let bytes = bus
                .read_bar(&mut bdf, options.bar_id, options.offset, options.size)
                .await
                .context("failed to call read")?
                .map_err(Status::from_raw)
                .with_context(|| {
                    format!("Couldn't read device {} bar {}", options.device, options.bar_id)
                })?;
            println!("read 0x{:x} bytes starting from 0x{:x}", bytes.len(), options.offset);
            print!(
                "{}",
                Hexdumper { bytes: &bytes, show_ascii: true, offset: Some(options.offset) }
            );
        }
        None => {
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

async fn find_bus_containing_bdf<'a>(
    filter: &'a Filter,
    proxies: &'a Vec<BusProxy>,
) -> Result<&'a BusProxy, Error> {
    for proxy in proxies {
        let info = proxy.get_host_bridge_info().await?;
        if info.start_bus_number <= filter.bus && info.end_bus_number >= filter.bus {
            return Ok(proxy);
        }
    }
    Err(anyhow!("PCI bus containing {} not found", filter))
}

// Find 'bus' entries that correspond to fuchsia.hardware.pci services.
async fn find_buses(path: &str) -> Result<Vec<BusProxy>, Error> {
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
            let (proxy, server) = fidl::endpoints::create_proxy::<BusMarker>()?;
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
