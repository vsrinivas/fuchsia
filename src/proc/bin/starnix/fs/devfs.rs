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

    let mkchr = |name, major, minor| {
        root.mknod(
            name,
            FileMode::IFCHR | FileMode::from_bits(0o666),
            DeviceType::new(major, minor),
        )
        .unwrap();
    };

    mkchr(b"null", 1, 3);
    mkchr(b"zero", 1, 5);
    mkchr(b"full", 1, 7);
    mkchr(b"random", 1, 8);
    mkchr(b"urandom", 1, 9);

    fs
}
