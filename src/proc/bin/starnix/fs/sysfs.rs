// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::task::*;
use crate::types::*;

struct SysFs;
impl FileSystemOps for SysFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs { f_type: SYSFS_MAGIC as i64, ..Default::default() })
    }
}

impl SysFs {
    fn new() -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(SysFs);
        StaticDirectoryBuilder::new(&fs)
            .add_node_entry(
                b"fs",
                StaticDirectoryBuilder::new(&fs)
                    .set_mode(mode!(IFDIR, 0o755))
                    .add_node_entry(
                        b"selinux",
                        StaticDirectoryBuilder::new(&fs).set_mode(mode!(IFDIR, 0o755)).build(),
                    )
                    .build(),
            )
            .build_root();
        Ok(fs)
    }
}

pub fn sys_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.sys_fs.get_or_init(|| SysFs::new().expect("failed to construct sysfs!"))
}
