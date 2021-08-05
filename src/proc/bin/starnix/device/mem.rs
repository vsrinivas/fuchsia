// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fd_impl_seekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn open_mem_device(minor: u32) -> Result<Box<dyn FileOps>, Errno> {
    match minor {
        DevNull::MINOR => Ok(Box::new(DevNull)),
        DevZero::MINOR => Ok(Box::new(DevZero)),
        _ => Err(ENODEV),
    }
}

pub struct DevNull;

impl DevNull {
    const MINOR: u32 = 3;
}

impl FileOps for DevNull {
    fd_impl_seekable!();

    fn write_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(UserBuffer::get_total_length(data))
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }
}

struct DevZero;

impl DevZero {
    pub const MINOR: u32 = 5;
}

impl FileOps for DevZero {
    fd_impl_seekable!();

    fn write_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(UserBuffer::get_total_length(data))
    }

    fn read_at(
        &self,
        _file: &FileObject,
        task: &Task,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut actual = 0;
        task.mm.write_each(data, |bytes| {
            actual += bytes.len();
            Ok(bytes)
        })?;
        Ok(actual)
    }
}
