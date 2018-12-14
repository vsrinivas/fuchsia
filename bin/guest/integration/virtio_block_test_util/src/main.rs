// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use fdio::{fdio_sys, ioctl, make_ioctl};
use fuchsia_zircon as zx;
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::mem;
use std::os::raw;
use std::path::Path;
use std::ptr;
use std::vec;
use structopt::StructOpt;

const BLOCK_DEV_DIR: &str = "/dev/class/block";

const IOCTL_BLOCK_GET_INFO: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_BLOCK,
    1
);

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct ioctl_block_info_t {
    pub block_count: u64,
    pub block_size: u32,
    pub max_transfer_size: u32,
    pub flags: u32,
    pub reserved: u32,
}

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
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(entry?.path())?;
        let mut block_info = ioctl_block_info_t {
            block_count: 0,
            block_size: 0,
            max_transfer_size: 0,
            flags: 0,
            reserved: 0,
        };
        let block_info_ptr = &mut block_info as *mut _ as *mut ::std::os::raw::c_void;
        unsafe {
            ioctl(
                &file,
                IOCTL_BLOCK_GET_INFO,
                ptr::null(),
                0,
                block_info_ptr,
                mem::size_of::<ioctl_block_info_t>(),
            )
        }
        .map_err(|_| zx::Status::IO)?;
        if block_info.block_size == block_size && block_info.block_count == block_count {
            return Ok(file);
        }
    }
    Err(zx::Status::NOT_FOUND)
}

fn read_block(
    block_dev: &mut File, block_size: u32, offset: u64, expected: u8,
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
    block_dev: &mut File, block_size: u32, offset: u64, value: u8,
) -> Result<(), zx::Status> {
    block_dev.seek(SeekFrom::Start(offset * block_size as u64))?;
    let data: Vec<u8> = vec![value; block_size as usize];
    block_dev.write_all(&data)?;
    block_dev.sync_all()?;
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
