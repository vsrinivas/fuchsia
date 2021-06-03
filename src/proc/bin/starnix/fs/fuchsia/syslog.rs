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
    pub fn new() -> FileHandle {
        FileObject::new(SyslogFile)
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

    fn fstat(&self, _file: &FileObject, task: &Task) -> Result<stat_t, Errno> {
        // TODO(tbodt): Replace these random numbers with an anonymous inode
        Ok(stat_t {
            st_dev: 0x16,
            st_ino: 3,
            st_nlink: 1,
            st_mode: 0x2190,
            st_uid: task.creds.uid,
            st_gid: task.creds.gid,
            st_rdev: 0x8800,
            ..stat_t::default()
        })
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
