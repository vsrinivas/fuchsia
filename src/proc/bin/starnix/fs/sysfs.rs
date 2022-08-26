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
        Ok(statfs { f_type: SYSFS_MAGIC as i64, ..Default::default() })
    }
}

impl SysFs {
    fn new_fs(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(kernel, SysFs);
        StaticDirectoryBuilder::new(&fs)
            .add_node_entry(
                b"fs",
                StaticDirectoryBuilder::new(&fs)
                    .set_mode(mode!(IFDIR, 0o755))
                    .add_node_entry(
                        b"selinux",
                        StaticDirectoryBuilder::new(&fs).set_mode(mode!(IFDIR, 0o755)).build(),
                    )
                    .add_node_entry(
                        b"cgroup",
                        fs.create_node(
                            CgroupDirectoryNode::new(),
                            mode!(IFDIR, 0o755),
                            FsCred::root(),
                        ),
                    )
                    .add_node_entry(
                        b"fuse",
                        StaticDirectoryBuilder::new(&fs)
                            .set_mode(mode!(IFDIR, 0o755))
                            .add_node_entry(
                                b"connections",
                                StaticDirectoryBuilder::new(&fs)
                                    .set_mode(mode!(IFDIR, 0o755))
                                    .build(),
                            )
                            .build(),
                    )
                    .build(),
            )
            .build_root();
        Ok(fs)
    }
}

pub fn sys_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.sys_fs.get_or_init(|| SysFs::new_fs(kern).expect("failed to construct sysfs!"))
}
