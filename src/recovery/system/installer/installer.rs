// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::partition::Partition,
    anyhow::{anyhow, Context, Error},
    fdio,
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_paver::{
        BootManagerMarker, Configuration, DynamicDataSinkProxy, PaverMarker, PaverProxy,
    },
    fidl_fuchsia_sysinfo::SysInfoMarker,
    fuchsia_component::client,
    fuchsia_zircon as zx,
    recovery_util::block::BlockDevice,
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BootloaderType {
    Efi,
    Coreboot,
}

pub async fn find_install_source(
    block_devices: &Vec<BlockDevice>,
    bootloader: BootloaderType,
) -> Result<&BlockDevice, Error> {
    let mut candidate = Err(anyhow!("Could not find the installer disk. Is it plugged in?"));
    for device in block_devices.iter().filter(|d| d.is_disk()) {
        // get_partitions returns an empty vector if it doesn't find any partitions
        // with the workstation-installer GUID on the disk.
        let partitions = Partition::get_partitions(device, block_devices, bootloader).await?;
        if !partitions.is_empty() {
            if candidate.is_err() {
                candidate = Ok(device);
            } else {
                return Err(anyhow!(
                    "Please check you only have one installation disk plugged in!"
                ));
            }
        }
    }
    candidate
}

pub fn paver_connect(path: &str) -> Result<(PaverProxy, DynamicDataSinkProxy), Error> {
    let (block_device_chan, block_remote) = zx::Channel::create()?;
    fdio::service_connect(&path, block_remote)?;
    let (data_sink_chan, data_remote) = zx::Channel::create()?;

    let paver: PaverProxy =
        client::connect_to_protocol::<PaverMarker>().context("Could not connect to paver")?;
    paver.use_block_device(ClientEnd::from(block_device_chan), ServerEnd::from(data_remote))?;

    let data_sink =
        DynamicDataSinkProxy::from_channel(fidl::AsyncChannel::from_channel(data_sink_chan)?);
    Ok((paver, data_sink))
}

pub async fn get_bootloader_type() -> Result<BootloaderType, Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<SysInfoMarker>()
        .context("Could not connect to 'fuchsia.sysinfo.SysInfo' service")?;
    let (status, bootloader) =
        proxy.get_bootloader_vendor().await.context("Getting bootloader vendor")?;
    if let Some(bootloader) = bootloader {
        println!("Bootloader vendor = {}", bootloader);
        if bootloader == "coreboot" {
            Ok(BootloaderType::Coreboot)
        } else {
            // The installer only supports coreboot and EFI,
            // and EFI BIOS vendor depends on the manufacturer,
            // so we assume that non-coreboot bootloader vendors
            // mean EFI.
            Ok(BootloaderType::Efi)
        }
    } else {
        Err(Error::new(zx::Status::from_raw(status)))
    }
}

/// Set the active boot configuration for the newly-installed system. We always boot from the "A"
/// slot to start with.
pub async fn set_active_configuration(paver: &PaverProxy) -> Result<(), Error> {
    let (boot_manager, server) = fidl::endpoints::create_proxy::<BootManagerMarker>()
        .context("Creating boot manager endpoints")?;

    paver.find_boot_manager(server).context("Could not find boot manager")?;

    zx::Status::ok(
        boot_manager
            .set_configuration_active(Configuration::A)
            .await
            .context("Sending set configuration active")?,
    )
    .context("Setting active configuration")?;

    zx::Status::ok(boot_manager.flush().await.context("Sending boot manager flush")?)
        .context("Flushing active configuration")
}
