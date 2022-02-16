// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::partition::Partition,
    anyhow::{anyhow, Context, Error},
    fdio,
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block::BlockProxy,
    fidl_fuchsia_paver::{
        BootManagerMarker, Configuration, DynamicDataSinkProxy, PaverMarker, PaverProxy,
    },
    fidl_fuchsia_sysinfo as fsysinfo,
    fuchsia_component::client,
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    std::{fs, path::Path},
};

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BootloaderType {
    Efi,
    Coreboot,
}

async fn connect_to_service(path: &str) -> Result<fidl::AsyncChannel, Error> {
    let (local, remote) = zx::Channel::create().context("Creating channel")?;
    fdio::service_connect(path, remote).context("Connecting to service")?;
    let local = fidl::AsyncChannel::from_channel(local).context("Creating AsyncChannel")?;
    Ok(local)
}

async fn block_device_get_info(
    block_channel: fidl::AsyncChannel,
) -> Result<Option<(String, u64)>, Error> {
    // Figure out topological path of the block device, so we can guess if it's a disk or a
    // partition.
    let (maybe_path, block_channel) = get_topological_path(block_channel).await?;
    let topo_path = maybe_path.ok_or(anyhow!("Failed to get topo path for device"))?;

    // partitions have paths like this:
    // /dev/sys/platform/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block/part-000/block
    // while disks are like this:
    // /dev/sys/platform/pci/00:17.0/ahci/sata2/block
    if topo_path.contains("/block/part-") {
        // This is probably a partition, skip it
        return Ok(None);
    }

    if topo_path.contains("/ramdisk-") {
        // This is probably ram, skip it
        return Ok(None);
    }

    let block = BlockProxy::from_channel(block_channel);
    let (status, maybe_info) = block.get_info().await?;
    if let Some(info) = maybe_info {
        let blocks = info.block_count;
        let block_size = info.block_size as u64;
        return Ok(Some((topo_path, blocks * block_size)));
    }

    return Err(Error::new(zx_status::Status::from_raw(status)));
}

// There's no nice way to use a service without losing the channel,
// so this function returns the controller.
async fn get_topological_path(
    channel: fidl::AsyncChannel,
) -> Result<(Option<String>, fidl::AsyncChannel), Error> {
    let controller = ControllerProxy::from_channel(channel);
    let topo_resp = controller.get_topological_path().await.context("Getting topological path")?;
    Ok((topo_resp.ok(), controller.into_channel().unwrap()))
}

pub async fn get_block_devices() -> Result<Vec<(String, u64)>, Error> {
    let block_dir = Path::new("/dev/class/block");
    let mut devices: Vec<(String, u64)> = Vec::new();
    for entry in fs::read_dir(block_dir)? {
        let block_channel = connect_to_service(entry?.path().to_str().unwrap()).await?;
        let result = block_device_get_info(block_channel).await?;
        if let Some(device) = result {
            devices.push(device);
        }
    }
    Ok(devices)
}

pub async fn find_install_source(
    block_devices: Vec<&String>,
    bootloader: BootloaderType,
) -> Result<&String, Error> {
    let mut candidate = Err(anyhow!("Could not find the installer disk. Is it plugged in?"));
    for device in block_devices.iter() {
        // get_partitions returns an empty vector if it doesn't find any partitions
        // with the workstation-installer GUID on the disk.
        let partitions = Partition::get_partitions(&device, bootloader).await?;
        if !partitions.is_empty() {
            if candidate.is_err() {
                candidate = Ok(*device);
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
    let (sysinfo_chan, remote) = zx::Channel::create()?;
    fdio::service_connect(&"/dev/sys/platform", remote).context("Connect to sysinfo")?;
    let sysinfo =
        fsysinfo::SysInfoProxy::from_channel(fidl::AsyncChannel::from_channel(sysinfo_chan)?);
    let (status, bootloader) =
        sysinfo.get_bootloader_vendor().await.context("Getting bootloader vendor")?;
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
