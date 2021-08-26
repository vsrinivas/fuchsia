// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;

use crate::fs::*;
use crate::task::*;
use crate::types::*;
pub struct EpollFileObject;

impl EpollFileObject {
    pub fn new(kernel: &Kernel) -> FileHandle {
        Anon::new_file(anon_fs(kernel), Box::new(EpollFileObject {}), OpenFlags::RDWR)
    }
}

impl FileOps for EpollFileObject {
    fd_impl_nonseekable!();

    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
