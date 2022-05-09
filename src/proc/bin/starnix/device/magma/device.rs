// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::file::MagmaFile;
use crate::device::DeviceOps;
use crate::fs::{FileOps, FsNode};
use crate::task::CurrentTask;
use crate::types::{DeviceType, Errno, OpenFlags};

pub struct MagmaDev {}

impl MagmaDev {
    pub fn new() -> Self {
        MagmaDev {}
    }
}

impl DeviceOps for MagmaDev {
    fn open(
        &self,
        _current_task: &CurrentTask,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        MagmaFile::new()
    }
}
