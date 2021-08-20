// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errno;
use crate::error;
use crate::fd_impl_nonseekable;
use crate::fs::FileObject;
use crate::fs::FileOps;
use crate::fs::SeekOrigin;
use crate::task::Task;
use crate::types::*;

/// A structure whose FileOps match the null file object.
pub struct NullFile;

impl FileOps for NullFile {
    fd_impl_nonseekable!();

    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
