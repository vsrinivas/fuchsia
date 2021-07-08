// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::task::Kernel;
use crate::types::*;

#[derive(Hash, PartialEq, Eq, Debug, Copy, Clone)]
pub enum AnonNodeType {
    Pipe,
    Misc,
}

// The () is to prevent construction from outside of this module
pub struct Anon(());

impl Anon {
    pub fn new_node(kernel: &Kernel, name: AnonNodeType) -> FsNodeHandle {
        FsNode::new_orphan(Anon(()), 0600, kernel.devices.get_anon_node_device(name))
    }
    pub fn new_file<T: FileOps + 'static>(
        kernel: &Kernel,
        ops: T,
        name: AnonNodeType,
    ) -> FileHandle {
        FileObject::new_unmounted(ops, Self::new_node(kernel, name))
    }
}

impl FsNodeOps for Anon {
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}
