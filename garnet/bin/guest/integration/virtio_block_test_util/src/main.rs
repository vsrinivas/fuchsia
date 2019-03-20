// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use fdio::clone_channel;
use fidl_fuchsia_hardware_block::BlockSynchronousProxy;
use fuchsia_zircon as zx;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;
use std::vec;
use structopt::StructOpt;

const BLOCK_DEV_DIR: &str = "/dev/class/block";

#[derive(StructOpt, Debug)]
struct Config {
    block_size: u32,
    block_count: u64,
    #[structopt(subcommand)]
    cmd: Command,
}

#[derive(StructOpt, Debug)]
enum Command {
    #[structopt(name = "check")]
    Check,
    #[structopt(name = "read")]
    Read { offset: u64, expected: u8 },
    #[structopt(name = "write")]
    Write { offset: u64, value: u8 },
}

fn find_block_device(block_size: u32, block_count: u64) -> Result<File, zx::Status> {
    let dir = Path::new(BLOCK_DEV_DIR);
    if !dir.is_dir() {
        return Err(zx::Status::IO);
    }
    for entry in fs::read_dir(dir)? {
        let file = OpenOptions::new().read(true).write(true).open(entry?.path())?;

        let channel = clone_channel(&file)?;
        let mut device = BlockSynchronousProxy::new(channel);
        let (status, maybe_block_info) =
            device.get_info(zx::Time::INFINITE).map_err(|_| zx::Status::IO)?;
        let block_info = maybe_block_info.ok_or(zx::Status::ok(status))?;
        if block_info.block_size == block_size && block_info.block_count == block_count {
            return Ok(file);
        }
    }
    Err(zx::Status::NOT_FOUND)
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
    // TODO(ZX-3294): We may want to support sync through the block
    // protocol, but in the interim, it is unsupported.
    // block_dev.sync_all()?;
    Ok(())
}

fn main() -> Result<(), zx::Status> {
    let config = Config::from_args();
    let mut block_dev = find_block_device(config.block_size, config.block_count)?;
    let result = match config.cmd {
        Command::Check => Ok(()),
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
