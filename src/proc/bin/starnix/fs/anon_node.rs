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
        FsNode::new_orphan(
            Anon(()),
            FileMode::from_bits(0o600),
            kernel.devices.get_anon_node_device(name),
        )
    }
    pub fn new_file(
        kernel: &Kernel,
        ops: Box<dyn FileOps>,
        name: AnonNodeType,
        flags: OpenFlags,
    ) -> FileHandle {
        FileObject::new_anonymous(ops, Self::new_node(kernel, name), flags)
    }
}

impl FsNodeOps for Anon {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
}
