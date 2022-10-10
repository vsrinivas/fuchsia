// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{StreamExt as _, TryStreamExt as _};
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::vec;
use structopt::StructOpt;

const BLOCK_CLASS_PATH: &str = "/dev/class/block/";

#[derive(StructOpt, Debug)]
struct Config {
    block_size: u32,
    pci_bus: u8,
    pci_device: u8,
    #[structopt(subcommand)]
    cmd: Command,
}

#[derive(StructOpt, Debug)]
enum Command {
    #[structopt(name = "check")]
    Check { block_count: u64 },
    #[structopt(name = "read")]
    Read { offset: u64, expected: u8 },
    #[structopt(name = "write")]
    Write { offset: u64, value: u8 },
}

// This tool has access to block nodes in the /dev/class/block path, but we need
// to match against composite PCI devices which sit in the /dev/ root we cannot
// get blanket access to. To achieve this, we walk all the block devices and
// look up their corresponding topological path to see if it corresponds to the
// device we're looking for.
async fn get_class_path_from_topological(
    topological_path_suffix: &str,
) -> Result<PathBuf, anyhow::Error> {
    // Scan current files in the directory and watch for new ones in case we
    // don't find the block device we're looking for. There's a slim possibility
    // a partition may not yet be available when this tool is executed in a
    // test.
    let block_dir = fuchsia_fs::directory::open_in_namespace(
        BLOCK_CLASS_PATH,
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )?;
    let watcher = fuchsia_vfs_watcher::Watcher::new(block_dir).await?;
    watcher
        .try_filter_map(|fuchsia_vfs_watcher::WatchMessage { event, filename }| {
            futures::future::ready((|| match event {
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                | fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                    let block_path = PathBuf::from(BLOCK_CLASS_PATH).join(filename);
                    let block_dev = File::open(&block_path)?;
                    let topo_path = fdio::device_get_topo_path(&block_dev)?;
                    Ok(topo_path.ends_with(topological_path_suffix).then(|| block_path))
                }
                _ => Ok(None),
            })())
        })
        .err_into()
        .next()
        .await
        .unwrap_or(Err(zx::Status::NOT_FOUND).map_err(Into::into))
}

async fn open_block_device(pci_bus: u8, pci_device: u8) -> Result<File, anyhow::Error> {
    // The filename is in the format pci-<bus>:<device>.<function>. The function
    // is always zero for virtio block devices.
    let topo_path_suffix =
        format!("/pci-{:02}:{:02}.0-fidl/virtio-block/block", pci_bus, pci_device);

    let class_path = get_class_path_from_topological(&topo_path_suffix).await?;
    let file = OpenOptions::new().read(true).write(true).open(&class_path)?;

    Ok(file)
}

async fn check(block_dev: &File, block_size: u32, block_count: u64) -> Result<(), anyhow::Error> {
    let channel = fdio::clone_channel(block_dev)?;
    let channel = fasync::Channel::from_channel(channel)?;
    let device = fidl_fuchsia_hardware_block::BlockProxy::new(channel);
    let (status, maybe_block_info) = device.get_info().await?;
    let () = zx::Status::ok(status)?;
    let block_info = maybe_block_info.ok_or(zx::Status::BAD_STATE)?;
    if block_info.block_size == block_size && block_info.block_count == block_count {
        Ok(())
    } else {
        Err(zx::Status::BAD_STATE)
    }
    .map_err(Into::into)
}

fn read_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    expected: u8,
) -> Result<(), anyhow::Error> {
    block_dev.seek(SeekFrom::Start(offset * block_size as u64))?;
    let mut data: Vec<u8> = vec![0; block_size as usize];
    block_dev.read_exact(&mut data)?;
    if data.iter().all(|&b| b == expected) { Ok(()) } else { Err(zx::Status::BAD_STATE) }
        .map_err(Into::into)
}

fn write_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    value: u8,
) -> Result<(), anyhow::Error> {
    block_dev.seek(SeekFrom::Start(offset * block_size as u64))?;
    let data: Vec<u8> = vec![value; block_size as usize];
    block_dev.write_all(&data)?;
    // TODO(fxbug.dev/33099): We may want to support sync through the block
    // protocol, but in the interim, it is unsupported.
    // block_dev.sync_all()?;
    Ok(())
}

#[fuchsia::main]
async fn main() -> Result<(), anyhow::Error> {
    let config = Config::from_args();
    let mut block_dev = open_block_device(config.pci_bus, config.pci_device).await?;
    let result = match config.cmd {
        Command::Check { block_count } => check(&block_dev, config.block_size, block_count).await,
        Command::Read { offset, expected } => {
            read_block(&mut block_dev, config.block_size, offset, expected)
        }
        Command::Write { offset, value } => {
            write_block(&mut block_dev, config.block_size, offset, value)
        }
    };
    match result.as_ref() {
        Ok(()) => {
            println!("PASS")
        }
        Err(err) => {
            println!("FAIL: {:#?}", err)
        }
    }
    result
}
