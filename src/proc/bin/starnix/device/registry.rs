// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::null::*;
use crate::fs::*;
use crate::types::*;

const MEM: u32 = 1;

fn open_mem_device(minor: u32) -> Result<Box<dyn FileOps>, Errno> {
    match minor {
        DevNull::MINOR => Ok(Box::new(DevNull)),
        _ => Err(ENODEV),
    }
}

pub fn open_character_device(dev: DeviceType) -> Result<Box<dyn FileOps>, Errno> {
    match dev.major() {
        MEM => open_mem_device(dev.minor()),
        _ => Err(ENODEV),
    }
}

pub fn open_block_device(_dev: DeviceType) -> Result<Box<dyn FileOps>, Errno> {
    Err(ENODEV)
}
