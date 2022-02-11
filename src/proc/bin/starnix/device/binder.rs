// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::WithStaticDeviceId;
use crate::fs::{FileObject, FileOps, FsNode, FsNodeOps, SeekOrigin};
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::CurrentTask;
use crate::types::*;
use crate::{error, fd_impl_nonblocking};

/// Android's binder kernel driver implementation.
#[derive(Clone)]
pub struct DevBinder;

impl DevBinder {
    /// The binder's static device ID. Use a MISC major number, which is the category used for
    /// non-specific hardware drivers.
    /// This could be dynamically assigned by the kernel but we don't have enough drivers nor the
    /// ability to dynamically load drivers to need this yet.
    pub const DEVICE_ID: DeviceType = DeviceType::new(10, 0);

    pub fn new() -> Self {
        Self
    }
}

/// The ioctl character for all binder ioctls.
const BINDER_IOCTL_CHAR: u8 = b'b';

/// The ioctl for retrieving the kernel binder version.
const BINDER_VERSION_IOCTL: u32 = encode_ioctl_write_read::<binder_version>(BINDER_IOCTL_CHAR, 9);

impl FileOps for DevBinder {
    fd_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            BINDER_VERSION_IOCTL => {
                if in_addr.is_null() {
                    return error!(EINVAL);
                }
                let response =
                    binder_version { protocol_version: BINDER_CURRENT_PROTOCOL_VERSION as i32 };
                current_task.mm.write_object(UserRef::new(in_addr), &response)?;
                Ok(SUCCESS)
            }
            _ => {
                log::error!("binder received unknown ioctl request 0x{:08x}", request);
                error!(EINVAL)
            }
        }
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn seek(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: off_t,
        _whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        error!(EOPNOTSUPP)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }
}

impl WithStaticDeviceId for DevBinder {
    const ID: DeviceType = Self::DEVICE_ID;
}

impl FsNodeOps for DevBinder {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(self.clone()))
    }
}
