// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::task::*;

struct SeLinuxFs;
impl FileSystemOps for SeLinuxFs {}
impl SeLinuxFs {
    fn new() -> FileSystemHandle {
        let fs = FileSystem::new(SeLinuxFs, FsNode::new_root(ROMemoryDirectory), None, true);
        fs
    }
}

pub fn selinux_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.selinux_fs.get_or_init(|| SeLinuxFs::new())
}
