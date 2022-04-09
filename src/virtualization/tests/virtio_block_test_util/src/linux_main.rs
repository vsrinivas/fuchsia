// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use libc;
use std::fs::{File, OpenOptions};
use std::io::{Error, ErrorKind, Read, Seek, SeekFrom, Write};
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
use structopt::StructOpt;

const PCI_DIR: &str = "/dev/disk/by-path";

#[cfg(target_arch = "aarch64")]
const PCI_DEVICE: &str = "platform-808100000.pci-pci-0000";

#[cfg(target_arch = "x86_64")]
const PCI_DEVICE: &str = "virtio-pci-0000";

// Ioctl requests are comprised of 32 bits:
// [31:30]  Access mode
// [29:16]  Size of the parameter structure
// [15:8]   Request type
// [7:0]    Request number
// For the ioctls we care about here there are no parameters, so we set only a type and a number.
// See Linux:include/uapi/asm-generic/ioctl.h for more details.
const TYPE_SHIFT: usize = 8;
macro_rules! define_ioctl {
    ($name:ident, $typ:expr, $num:expr, $return_type:ty) => {
        fn $name(file: &File) -> $return_type {
            let request = ($typ << TYPE_SHIFT) | ($num & 0xff);
            let mut r: $return_type = 0;
            unsafe {
                libc::ioctl(file.as_raw_fd(), request, &mut r);
            }
            r
        }
    };
}

// Block ioctl types and number are defined in Linux:include/uapi/linux/fs.h.
define_ioctl!(block_dev_size, 0x12, 96, u64);
define_ioctl!(block_dev_sector_size, 0x12, 104, u32);

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

fn open_block_device(pci_bus: u8, pci_device: u8, write: bool) -> std::io::Result<File> {
    let mut path = PathBuf::from(PCI_DIR);
    path.push(format!("{}:{:02}:{:02}.0", PCI_DEVICE, pci_bus, pci_device));
    OpenOptions::new().read(true).write(write).open(path)
}

fn check(block_dev: &File, block_size: u32, block_count: u64) -> std::io::Result<()> {
    let actual_block_size = block_dev_sector_size(block_dev);
    let actual_block_count = block_dev_size(block_dev);

    if block_size != actual_block_size {
        return Err(Error::new(
            ErrorKind::Other,
            format!("Incorrect size: {} (expected {}).", actual_block_size, block_size),
        ));
    }
    if block_count != actual_block_count {
        return Err(Error::new(
            ErrorKind::Other,
            format!("Incorrect count: {} (expected {}).", actual_block_count, block_count),
        ));
    }
    Ok(())
}

fn read_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    expected: u8,
) -> std::io::Result<()> {
    block_dev.seek(SeekFrom::Start(offset * u64::from(block_size)))?;
    let mut data: Vec<u8> = vec![0; block_size as usize];
    block_dev.read_exact(&mut data)?;
    if !data.iter().all(|&b| b == expected) {
        return Err(Error::new(ErrorKind::Other, "Incorrect data read"));
    }
    Ok(())
}

fn write_block(
    block_dev: &mut File,
    block_size: u32,
    offset: u64,
    value: u8,
) -> std::io::Result<()> {
    block_dev.seek(SeekFrom::Start(offset * u64::from(block_size)))?;
    let data: Vec<u8> = vec![value; block_size as usize];
    block_dev.write_all(&data)?;
    block_dev.sync_all()?;
    Ok(())
}

fn main() -> std::io::Result<()> {
    let config = Config::from_args();
    let write = if let Command::Write { .. } = config.cmd { true } else { false };
    let mut block_dev = open_block_device(config.pci_bus, config.pci_device, write)?;
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
