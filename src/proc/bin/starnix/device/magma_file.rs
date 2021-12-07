// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]

use std::sync::Arc;

use super::magma::*;
use crate::error;
use crate::fd_impl_nonblocking;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::syscalls::*;
use crate::task::{CurrentTask, EventHandler, Waiter};
use crate::types::*;

pub struct MagmaNode {}
impl FsNodeOps for MagmaNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        MagmaFile::new()
    }
}

pub struct MagmaFile {}

impl MagmaFile {
    pub fn new() -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(Self {}))
    }
}

impl FileOps for MagmaFile {
    fd_impl_nonseekable!();
    fd_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        task: &CurrentTask,
        _request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        let (command, command_type) = read_magma_command_and_type(task, in_addr)?;
        let _response_address = UserAddress::from(command.response_address);

        match command_type {
            t => {
                log::warn!("Got unknown request: {:?}", t);
                error!(ENOSYS)
            }
        }?;

        Ok(SUCCESS)
    }

    fn read(
        &self,
        _file: &FileObject,
        _task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
