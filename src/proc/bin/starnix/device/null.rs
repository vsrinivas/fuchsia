// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fd_impl_seekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub struct DevNull;

impl DevNull {
    pub const MINOR: u32 = 3;
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
        Ok(data.len())
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
