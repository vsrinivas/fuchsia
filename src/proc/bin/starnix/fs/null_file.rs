// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error;
use crate::fs::{fileops_impl_nonblocking, fileops_impl_nonseekable, FileObject, FileOps};
use crate::task::CurrentTask;
use crate::types::*;

/// A structure whose FileOps match the null file object.
pub struct NullFile;

impl FileOps for NullFile {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
