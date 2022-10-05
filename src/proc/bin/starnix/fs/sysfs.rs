// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::auth::FsCred;
use crate::fs::cgroup::CgroupDirectoryNode;
use crate::task::*;
use crate::types::*;

struct SysFs;
impl FileSystemOps for SysFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(SYSFS_MAGIC))
    }
}

impl SysFs {
    fn new_fs(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(kernel, SysFs);
        StaticDirectoryBuilder::new(&fs)
            .subdir(b"fs", 0o755, |dir| {
                dir.subdir(b"selinux", 0o755, |dir| dir)
                    .subdir(b"bpf", 0o755, |dir| dir)
                    .node(
                        b"cgroup",
                        fs.create_node(
                            CgroupDirectoryNode::new(),
                            mode!(IFDIR, 0o755),
                            FsCred::root(),
                        ),
                    )
                    .subdir(b"fuse", 0o755, |dir| dir.subdir(b"connections", 0o755, |dir| dir))
            })
            .build_root();
        Ok(fs)
    }
}

pub fn sys_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.sys_fs.get_or_init(|| SysFs::new_fs(kern).expect("failed to construct sysfs!"))
}
