// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn new_memfd(current_task: &CurrentTask, flags: OpenFlags) -> Result<FileHandle, Errno> {
    let kernel = current_task.kernel();
    let fs = anon_fs(kernel);
    let node = fs.create_node(Box::new(VmoFileNode::new()?), FileMode::from_bits(0o600));
    Ok(FileObject::new_anonymous(node.open(current_task, flags)?, node, flags))
}
