// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::task::Kernel;
use crate::types::*;

pub struct Anon;

impl FsNodeOps for Anon {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}

impl Anon {
    pub fn new_file(fs: &FileSystemHandle, ops: Box<dyn FileOps>, flags: OpenFlags) -> FileHandle {
        FileObject::new_anonymous(
            ops,
            FsNode::new(Box::new(Anon), FileMode::from_bits(0o600), fs),
            flags,
        )
    }
}

struct AnonFs;
impl FileSystemOps for AnonFs {}
pub fn anon_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.anon_fs.get_or_init(|| FileSystem::new_no_root(AnonFs))
}
