// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::cprng_draw;

use crate::device::DeviceOps;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

struct DevNull;
impl FileOps for DevNull {
    fileops_impl_seekless!();
    fileops_impl_nonblocking!();

    fn write_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        current_task.mm.read_each(data, |bytes| {
            tracing::info!(
                "{:?} write to devnull: {:?}",
                current_task,
                String::from_utf8_lossy(bytes)
            );
            Ok(Some(()))
        })?;
        UserBuffer::get_total_length(data)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }
}

struct DevZero;
impl FileOps for DevZero {
    fileops_impl_seekless!();
    fileops_impl_nonblocking!();

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        UserBuffer::get_total_length(data)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut actual = 0;
        current_task.mm.write_each(data, |bytes| {
            actual += bytes.len();
            Ok(bytes)
        })?;
        Ok(actual)
    }
}

struct DevFull;
impl FileOps for DevFull {
    fileops_impl_seekless!();
    fileops_impl_nonblocking!();

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(ENOSPC)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut actual = 0;
        current_task.mm.write_each(data, |bytes| {
            actual += bytes.len();
            Ok(bytes)
        })?;
        Ok(actual)
    }
}

struct DevRandom;
impl FileOps for DevRandom {
    fileops_impl_seekless!();
    fileops_impl_nonblocking!();

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        UserBuffer::get_total_length(data)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut actual = 0;
        current_task.mm.write_each(data, |bytes| {
            actual += bytes.len();
            cprng_draw(bytes);
            Ok(bytes)
        })?;
        Ok(actual)
    }
}

struct DevKmsg;
impl FileOps for DevKmsg {
    fileops_impl_seekless!();
    fileops_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let total = UserBuffer::get_total_length(data)?;
        let mut bytes = vec![0; total];
        current_task.mm.read_all(data, &mut bytes)?;
        tracing::info!(tag = "kmsg", "{}", String::from_utf8_lossy(&bytes).trim_end_matches('\n'));
        Ok(total)
    }
}

pub struct MemDevice;
impl DeviceOps for MemDevice {
    fn open(
        &self,
        _current_task: &CurrentTask,
        id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(match id.minor() {
            3 => Box::new(DevNull),
            5 => Box::new(DevZero),
            7 => Box::new(DevFull),
            8 | 9 => Box::new(DevRandom),
            11 => Box::new(DevKmsg),
            _ => return error!(ENODEV),
        })
    }
}

pub struct MiscDevice;
impl DeviceOps for MiscDevice {
    fn open(
        &self,
        _current_task: &CurrentTask,
        id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(match id.minor() {
            183 => Box::new(DevRandom),
            _ => return error!(ENODEV),
        })
    }
}
