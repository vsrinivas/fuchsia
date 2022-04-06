// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fdio::{clone_channel, watch_directory, WatchEvent};
use fidl_fuchsia_hardware_block::BlockSynchronousProxy;
use fuchsia_zircon as zx;
use std::fs::{File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::vec;
use structopt::StructOpt;
use zx::Status;

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
fn get_class_path_from_topological(topological_path: &PathBuf) -> Result<PathBuf, zx::Status> {
    let mut class_path = PathBuf::new();
    let watch_callback = |_: WatchEvent, filename: &Path| -> Result<(), Status> {
        let mut block_path = PathBuf::from(BLOCK_CLASS_PATH);
        block_path.push(filename);
        let block_dev = File::open(&block_path).or(Err(zx::Status::IO))?;
        let topo_path = PathBuf::from(fdio::device_get_topo_path(&block_dev)?);
        if topo_path == *topological_path {
            class_path = block_path.to_path_buf();
            Err(zx::Status::STOP)
        } else {
            Ok(())
        }
    };

    // Scan current files in the directory and watch for new ones in case we
    // don't find the block device we're looking for. There's a slim possibility
    // a partition may not yet be available when this tool is executed in a
    // test.
    let block_dir = File::open(BLOCK_CLASS_PATH)?;
    match watch_directory(&block_dir, zx::sys::ZX_TIME_INFINITE, watch_callback) {
        zx::Status::STOP => Ok(class_path),
        _ => Err(zx::Status::NOT_FOUND),
    }
}

fn open_block_device(pci_bus: u8, pci_device: u8) -> Result<File, zx::Status> {
    // The filename is in the format pci-<bus>:<device>.<function>. The function
    // is always zero for virtio block devices.
    let topo_path =
        PathBuf::from(format!("/dev/pci-{:02}:{:02}.0/virtio-block/block", pci_bus, pci_device));

    let class_path = get_class_path_from_topological(&topo_path)?;
    let file = OpenOptions::new().read(true).write(true).open(&class_path)?;

    Ok(file)
}

fn check(block_dev: &File, block_size: u32, block_count: u64) -> Result<(), zx::Status> {
    let channel = clone_channel(block_dev)?;
    let device = BlockSynchronousProxy::new(channel);
    let (status, maybe_block_info) =
        device.get_info(zx::Time::INFINITE).map_err(|_| zx::Status::IO)?;
    let block_info = maybe_block_info.ok_or(zx::Status::ok(status))?;
    if block_info.block_size == block_size && block_info.block_count == block_count {
        Ok(())
    } else {
        Err(zx::Status::BAD_STATE)
    }
}

fn read_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    expected: u8,
) -> Result<(), zx::Status> {
    block_dev.seek(SeekFrom::Start(offset * block_size as u64))?;
    let mut data: Vec<u8> = vec![0; block_size as usize];
    block_dev.read_exact(&mut data)?;
    if !data.iter().all(|&b| b == expected) {
        return Err(zx::Status::BAD_STATE);
    }
    Ok(())
}

fn write_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    value: u8,
) -> Result<(), zx::Status> {
    block_dev.seek(SeekFrom::Start(offset * block_size as u64))?;
    let data: Vec<u8> = vec![value; block_size as usize];
    block_dev.write_all(&data)?;
    // TODO(fxbug.dev/33099): We may want to support sync through the block
    // protocol, but in the interim, it is unsupported.
    // block_dev.sync_all()?;
    Ok(())
}

fn main() -> Result<(), zx::Status> {
    let config = Config::from_args();
    let mut block_dev = open_block_device(config.pci_bus, config.pci_device)?;
    let result = match config.cmd {
        Command::Check { block_count } => check(&block_dev, config.block_size, block_count),
        Command::Read { offset, expected } => {
            read_block(&mut block_dev, config.block_size, offset, expected)
        }
        Command::Write { offset, value } => {
            write_block(&mut block_dev, config.block_size, offset, value)
        }
    };
    if result.is_ok() {
        println!("PASS");
    }
    result
}
