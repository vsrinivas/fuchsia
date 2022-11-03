// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use tracing::info;

use crate::fs::*;
use crate::mm::MemoryAccessorExt;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub struct SyslogFile;

impl SyslogFile {
    pub fn new_file(current_task: &CurrentTask) -> FileHandle {
        Anon::new_file(current_task, Box::new(SyslogFile), OpenFlags::RDWR)
    }
}

impl FileOps for SyslogFile {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn write(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut size = 0;
        current_task.mm.read_each(data, |bytes| {
            info!(target: "stdio", "{}", String::from_utf8_lossy(bytes));
            size += bytes.len();
            Ok(Some(()))
        })?;
        Ok(size)
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Ok(0)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            TCGETS => error!(ENOTTY),
            _ => default_ioctl(current_task, request),
        }
    }
}
