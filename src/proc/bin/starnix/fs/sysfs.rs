// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::mode;
use crate::task::*;
use crate::types::*;

struct SysFs;
impl FileSystemOps for SysFs {}

impl SysFs {
    fn new() -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(SysFs);
        fs.set_root(ROMemoryDirectory);
        let root = fs.root();
        let dir_fs = root.add_node_ops(b"fs", mode!(IFDIR, 0o755), ROMemoryDirectory)?;
        let _fs_selinux = dir_fs.add_node_ops(b"selinux", mode!(IFDIR, 0755), ROMemoryDirectory)?;
        Ok(fs)
    }
}

pub fn sys_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.sys_fs.get_or_init(|| SysFs::new().expect("failed to construct sysfs!"))
}
