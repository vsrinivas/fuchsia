// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::tmpfs::*;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn dev_tmp_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.dev_tmp_fs.get_or_init(init_devfs)
}

fn init_devfs() -> FileSystemHandle {
    let fs = TmpFs::new();
    let root = fs.root();
    root.mknod(b"null", FileMode::IFCHR | FileMode::from_bits(0o666), DeviceType::new(1, 3))
        .unwrap();
    fs
}
