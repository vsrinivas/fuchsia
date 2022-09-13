// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::task::{CurrentTask, Kernel};
use crate::types::*;

pub struct Anon;

impl FsNodeOps for Anon {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        error!(ENOSYS)
    }
}

impl Anon {
    pub fn new_file(
        current_task: &CurrentTask,
        ops: Box<dyn FileOps>,
        flags: OpenFlags,
    ) -> FileHandle {
        let fs = anon_fs(current_task.kernel());
        FileObject::new_anonymous(
            ops,
            fs.create_node(Box::new(Anon), FileMode::from_bits(0o600), current_task.as_fscred()),
            flags,
        )
    }
}

struct AnonFs;
impl FileSystemOps for AnonFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(ANON_INODE_FS_MAGIC))
    }
}
pub fn anon_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.anon_fs.get_or_init(|| FileSystem::new(kernel, AnonFs))
}
