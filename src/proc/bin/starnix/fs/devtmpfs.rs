// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::tmpfs::*;
use crate::fs::*;
use crate::task::*;
use crate::types::*;
use std::sync::Arc;

pub fn dev_tmp_fs(kernel: &Arc<Kernel>) -> &FileSystemHandle {
    kernel.dev_tmp_fs.get_or_init(|| init_devtmpfs(kernel))
}

fn init_devtmpfs(kernel: &Arc<Kernel>) -> FileSystemHandle {
    let fs = TmpFs::new(kernel);
    let root = fs.root();

    let mkchr = |name, device_type| {
        root.create_node(name, FileMode::IFCHR | FileMode::from_bits(0o666), device_type).unwrap();
    };

    let mkdir = |name| {
        root.create_node(name, FileMode::IFDIR | FileMode::from_bits(0o755), DeviceType::NONE)
            .unwrap();
    };

    mkchr(b"null", DeviceType::NULL);
    mkchr(b"zero", DeviceType::ZERO);
    mkchr(b"full", DeviceType::FULL);
    mkchr(b"random", DeviceType::RANDOM);
    mkchr(b"urandom", DeviceType::URANDOM);

    mkdir(b"shm");

    // tty related nodes
    mkdir(b"pts");
    mkchr(b"tty", DeviceType::TTY);
    root.create_symlink(b"ptmx", b"pts/ptmx").unwrap();

    fs
}
