// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn new_memfd(kernel: &Kernel, flags: OpenFlags) -> Result<FileHandle, Errno> {
    let node = FsNode::new_orphan(VmoFileNode::new()?, FileMode::from_bits(0o600), anon_fs(kernel));
    Ok(FileObject::new_anonymous(node.open(flags)?, node, flags))
}
