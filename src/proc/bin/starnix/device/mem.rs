// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::cprng_draw;

use crate::device::WithStaticDeviceId;
use crate::error;
use crate::fd_impl_nonblocking;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

macro_rules! fd_impl_seekless {
    () => {
        fn read(
            &self,
            file: &FileObject,
            current_task: &CurrentTask,
            data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            self.read_at(file, current_task, 0, data)
        }
        fn write(
            &self,
            file: &FileObject,
            current_task: &CurrentTask,
            data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            self.write_at(file, current_task, 0, data)
        }
        fn seek(
            &self,
            _file: &FileObject,
            _current_task: &CurrentTask,
            _offset: off_t,
            _whence: SeekOrigin,
        ) -> Result<off_t, Errno> {
            Ok(0)
        }
    };
}

/// Implements the /dev/null driver.
pub struct DevNull;

impl WithStaticDeviceId for DevNull {
    const ID: DeviceType = DeviceType::NULL;
}

impl FsNodeOps for DevNull {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevNullFile))
    }
}

struct DevNullFile;

impl FileOps for DevNullFile {
    fd_impl_seekless!();
    fd_impl_nonblocking!();

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
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }
}

/// Implements the /dev/zero driver.
pub struct DevZero;

impl WithStaticDeviceId for DevZero {
    const ID: DeviceType = DeviceType::ZERO;
}

impl FsNodeOps for DevZero {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevZeroFile))
    }
}

struct DevZeroFile;

impl FileOps for DevZeroFile {
    fd_impl_seekless!();
    fd_impl_nonblocking!();

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

/// Implements the /dev/full driver.
pub struct DevFull;

impl WithStaticDeviceId for DevFull {
    const ID: DeviceType = DeviceType::FULL;
}

impl FsNodeOps for DevFull {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevFullFile))
    }
}

struct DevFullFile;

impl FileOps for DevFullFile {
    fd_impl_seekless!();
    fd_impl_nonblocking!();

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

/// Implements the /dev/random driver.
pub struct DevRandom;

impl WithStaticDeviceId for DevRandom {
    const ID: DeviceType = DeviceType::RANDOM;
}

impl FsNodeOps for DevRandom {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevRandomFile))
    }
}

/// Implements the /dev/urandom driver.
pub struct DevURandom;

impl WithStaticDeviceId for DevURandom {
    const ID: DeviceType = DeviceType::URANDOM;
}

impl FsNodeOps for DevURandom {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevRandomFile))
    }
}

struct DevRandomFile;

impl FileOps for DevRandomFile {
    fd_impl_seekless!();
    fd_impl_nonblocking!();

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

/// Implements the /dev/kmsg driver.
pub struct DevKmsg;

impl WithStaticDeviceId for DevKmsg {
    const ID: DeviceType = DeviceType::KMSG;
}

impl FsNodeOps for DevKmsg {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(DevKmsgFile))
    }
}

struct DevKmsgFile;

impl FileOps for DevKmsgFile {
    fd_impl_seekless!();
    fd_impl_nonblocking!();

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
        log::info!(target: "kmsg", "{}", String::from_utf8_lossy(&bytes).trim_end_matches('\n'));
        Ok(total)
    }
}
