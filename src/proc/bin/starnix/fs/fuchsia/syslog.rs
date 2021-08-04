// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::info;

use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub struct SyslogFile;

impl SyslogFile {
    #[cfg(test)]
    pub fn new(kernel: &Kernel) -> FileHandle {
        Anon::new_file(anon_fs(kernel), Box::new(SyslogFile), OpenFlags::RDWR)
    }
}

impl FileOps for SyslogFile {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut size = 0;
        task.mm.read_each(data, |bytes| {
            info!(target: "stdio", "{}", String::from_utf8_lossy(bytes));
            size += bytes.len();
            Ok(Some(()))
        })?;
        Ok(size)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        Ok(0)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _task: &Task,
        request: u32,
        _in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            TCGETS => Err(ENOTTY),
            _ => default_ioctl(request),
        }
    }
}
