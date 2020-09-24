// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod partition;
pub mod payload_streamer;

use {
    crate::partition::Partition,
    anyhow::{anyhow, Context, Error},
    fdio,
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_block::BlockProxy,
    fidl_fuchsia_paver::{DynamicDataSinkProxy, PaverMarker, PaverProxy},
    fidl_fuchsia_sysinfo as fsysinfo, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
    std::{
        fs,
        io::{self, BufRead, Write},
        path::Path,
    },
    structopt::StructOpt,
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

// There's no nice way to use a service without losing the channel,
// so this function returns the controller.
async fn get_topological_path(
    channel: fidl::AsyncChannel,
) -> Result<(Option<String>, fidl::AsyncChannel), Error> {
    let controller = ControllerProxy::from_channel(channel);
    let topo_resp = controller.get_topological_path().await.context("Getting topological path")?;
    Ok((topo_resp.ok(), controller.into_channel().unwrap()))
}

async fn block_device_get_info(
    block_channel: fidl::AsyncChannel,
) -> Result<Option<(String, u64)>, Error> {
    // Figure out topological path of the block device, so we can guess if it's a disk or a
    // partition.
    let (maybe_path, block_channel) = get_topological_path(block_channel).await?;
    let topo_path = maybe_path.ok_or(anyhow!("Failed to get topo path for device"))?;

    // partitions have paths like this:
    // /dev/sys/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block/part-000/block
    // while disks are like this:
    // /dev/sys/pci/00:17.0/ahci/sata2/block
    if topo_path.contains("/block/part-") {
        // This is probably a partition, skip it
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

fn get_user_selection(mut choices: Vec<&String>) -> Result<&String, Error> {
    print!("Enter a selection (0..{}): ", choices.len() - 1);
    io::stdout().flush()?;
    let stdin = io::stdin();
    let mut handle = stdin.lock();
    let selected = loop {
        let mut input = String::new();
        handle.read_line(&mut input).context("Reading stdin")?;
        print!("{}", input); // TODO(fxbug.dev/47155): why does echo not work?
        match input.trim().parse::<usize>() {
            Ok(selection) if selection < choices.len() => {
                break selection;
            }
            _ => {
                print!("Invalid selection, try again? (0..{}): ", choices.len() - 1);
                io::stdout().flush()?;
            }
        }
    };
    Ok(choices.swap_remove(selected))
}

async fn get_block_devices() -> Result<Vec<(String, u64)>, Error> {
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

fn select_block_device(devices: Vec<&(String, u64)>) -> Result<&String, Error> {
    if devices.is_empty() {
        return Err(anyhow!("Found no block devices."));
    }

    println!("Please select the disk you want to install Fuchsia to:");
    for (i, (topo_path, device_size)) in devices.iter().enumerate() {
        println!("[{}] {} ({}G)", i, topo_path, device_size / (1024 * 1024 * 1024));
    }
    get_user_selection(devices.iter().map(|(path, _)| path).collect())
}

async fn find_install_source(
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
                    "Found more than one possible installation source. Please check you only \
                        have one installation disk plugged in!"
                ));
            }
        }
    }

    candidate
}

fn paver_connect(path: &str) -> Result<(PaverProxy, DynamicDataSinkProxy), Error> {
    let (block_device_chan, block_remote) = zx::Channel::create()?;
    fdio::service_connect(&path, block_remote)?;
    let (data_sink_chan, data_remote) = zx::Channel::create()?;

    let paver: PaverProxy =
        client::connect_to_service::<PaverMarker>().context("Could not connect to paver")?;
    paver.use_block_device(ServerEnd::from(block_device_chan), ServerEnd::from(data_remote))?;

    let data_sink =
        DynamicDataSinkProxy::from_channel(fidl::AsyncChannel::from_channel(data_sink_chan)?);
    Ok((paver, data_sink))
}

async fn get_bootloader_type() -> Result<BootloaderType, Error> {
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

fn do_confirmation_prompt() -> Result<bool, Error> {
    print!("Do you wish to proceed? (yes/[no]) ");
    io::stdout().flush()?;
    // TODO(fxbug.dev/47155): Why does this not echo what you type in?
    let mut input = String::new();
    if let Err(e) = io::stdin().read_line(&mut input) {
        println!("Failed to read stdin: {}", e);
        return Err(Error::new(e));
    }
    print!("{}", input);

    if input.to_lowercase().trim() != "yes" {
        return Ok(false);
    }

    Ok(true)
}

#[derive(Debug, StructOpt)]
#[structopt(name = "installer", about = "install Fuchsia to a disk")]
struct Opt {
    /// Don't prompt before wiping the disk
    #[structopt(long = "no-wipe-prompt")]
    force: bool,

    /// Block device to install to
    #[structopt(name = "FILE")]
    block_device: Option<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    let block_devices = get_block_devices().await?;
    let bootloader_type = get_bootloader_type().await?;
    let install_source =
        find_install_source(block_devices.iter().map(|(part, _)| part).collect(), bootloader_type)
            .await?;
    let block_device_path = opt.block_device.as_ref().unwrap_or_else(|| {
        select_block_device(
            block_devices.iter().filter(|(part, _)| part != install_source).collect(),
        )
        .expect("Couldn't select a block device!")
    });
    println!("Using {} as installation target.", block_device_path);
    println!();
    println!(
        "WARNING: Installing Fuchsia will WIPE YOUR DISK. Make sure you've backed everything \
        up before proceeding!"
    );
    if !opt.force {
        if !do_confirmation_prompt()? {
            println!("Aborting!");
            return Ok(());
        }
    }

    if !block_device_path.starts_with("/dev/") {
        println!("Invalid block device path!");
        return Ok(());
    }

    let (_paver, data_sink) =
        paver_connect(&block_device_path).context("Could not contact paver")?;

    println!("Wiping old partition tables...");
    data_sink.wipe_partition_tables().await?;
    println!("Initializing Fuchsia partition tables...");
    data_sink.initialize_partition_tables().await?;
    println!("Success.");

    let to_install = Partition::get_partitions(&install_source, bootloader_type)
        .await
        .context("Getting source partitions")?;

    for part in to_install {
        print!("{:?}... ", part);
        io::stdout().flush()?;
        if part.pave(&data_sink).await.is_err() {
            println!("Failed");
        } else {
            println!("OK");

            if part.is_ab() {
                print!("{:?} [-B]... ", part);
                io::stdout().flush()?;
                if part.pave_b(&data_sink).await.is_err() {
                    println!("Failed");
                } else {
                    println!("OK");
                }
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_device::{ControllerRequest, ControllerRequestStream},
        fidl_fuchsia_hardware_block::{BlockInfo, BlockRequest, BlockRequestStream},
        futures::prelude::*,
    };

    async fn serve_block_device(
        full_path: &str,
        block_count: u64,
        block_size: u32,
        expect_get_info: bool,
        server_end: fidl::AsyncChannel,
    ) -> Result<(), Error> {
        // we expect one get_topological_path request,
        // and one get_info request.
        let mut stream = ControllerRequestStream::from_channel(server_end);
        let req = stream.try_next().await?.unwrap();
        if let ControllerRequest::GetTopologicalPath { responder } = req {
            responder.send(&mut Ok(full_path.to_string()))?;
        } else {
            panic!("Expected a GetTopologicalPath request, did not get one.");
        }

        if !expect_get_info {
            return Ok(());
        }

        let inner = stream.into_inner();
        let mut stream = BlockRequestStream::from_inner(inner.0, inner.1);
        let req = stream.try_next().await?;
        if !expect_get_info {
            return Ok(());
        }
        if let BlockRequest::GetInfo { responder } = req.unwrap() {
            responder.send(
                0,
                Some(&mut BlockInfo {
                    block_count: block_count,
                    block_size,
                    max_transfer_size: 0,
                    flags: 0,
                    reserved: 0,
                }),
            )?;
        } else {
            panic!("Expected a GetInfo request, but did not get one.");
        }
        Ok(())
    }

    fn mock_block_device(
        full_path: &'static str,
        block_count: u64,
        block_size: u32,
        expect_get_info: bool,
    ) -> Result<fidl::AsyncChannel, Error> {
        let (client, server) = zx::Channel::create()?;
        let client = fidl::AsyncChannel::from_channel(client)?;
        let server = fidl::AsyncChannel::from_channel(server)?;
        fasync::Task::local(
            serve_block_device(full_path, block_count, block_size, expect_get_info, server)
                .unwrap_or_else(|e| panic!("Error while serving fake block device: {}", e)),
        )
        .detach();
        Ok(client)
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_basic() -> Result<(), Error> {
        let chan = mock_block_device("/dev/sys/pci/00:17.0/ahci/sata2/block", 100, 512, true)?;
        let info = block_device_get_info(chan).await?;
        assert!(info.is_some());

        let (path, size) = info.unwrap();
        assert_eq!(path, "/dev/sys/pci/00:17.0/ahci/sata2/block");
        assert_eq!(size, 100 * 512);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_weird_block_size() -> Result<(), Error> {
        let chan = mock_block_device(
            "/dev/sys/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block",
            1000,
            4,
            true,
        )?;
        let info = block_device_get_info(chan).await?;
        assert!(info.is_some());

        let (path, size) = info.unwrap();
        assert_eq!(path, "/dev/sys/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block");
        assert_eq!(size, 1000 * 4);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_partitions() -> Result<(), Error> {
        let chan =
            mock_block_device("/dev/sys/pci/00:17.0/ahci/sata2/block/part-000/block", 0, 0, false)?;
        let info = block_device_get_info(chan).await?;
        assert!(info.is_none());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_usb_partitions() -> Result<(), Error> {
        let chan = mock_block_device(
            "/dev/sys/pci/00:14.0/xhci/usb-bus/001/001/ifc-000/ums/lun-000/block/part-003/block",
            0,
            0,
            false,
        )?;
        let info = block_device_get_info(chan).await?;
        assert!(info.is_none());
        Ok(())
    }
}
