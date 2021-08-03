// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_driver_lspci_args::DriverLspci,
    fidl::endpoints::Proxy, fidl_fuchsia_io::DirectoryProxy, lspci::bridge::Bridge,
    lspci::device::Device, lspci::Args, zstd::block::decompress,
};

#[ffx_plugin("driver_enabled", DirectoryProxy = "bootstrap/driver_manager:expose:dev")]

pub async fn lspci(dev: DirectoryProxy, cmd: DriverLspci) -> Result<()> {
    // Creates the proxy and server
    let (proxy, server) = fidl::endpoints::create_proxy::<fidl_fuchsia_io::NodeMarker>()?;

    dev.open(
        fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        0,
        &cmd.service,
        server,
    )?;

    let bus = fidl_fuchsia_hardware_pci::BusProxy::new(proxy.into_channel().unwrap());
    let pci_ids = include_bytes!("../../../../../../../third_party/pciids/pci.ids.zst");
    // The capacity to 2 MB, because the decompressed data
    // should always be less than the capacity's bytes
    let pci_ids = String::from_utf8(decompress(pci_ids, 2_097_152)?)?;
    let db = Some((lspci::db::PciDb::new(&pci_ids))?);

    let args = Args {
        service: cmd.service,
        verbose: cmd.verbose,
        quiet: cmd.quiet,
        print_config: cmd.print_config,
        print_numeric: cmd.print_numeric,
        only_print_numeric: cmd.only_print_numeric,
        filter: cmd.filter,
    };

    // Prints PCI Info
    for fidl_device in &bus.get_devices().await? {
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
