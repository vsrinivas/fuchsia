// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth::FsCred;
use crate::fs::tmpfs::*;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub fn dev_tmp_fs(task: &CurrentTask) -> &FileSystemHandle {
    task.kernel().dev_tmp_fs.get_or_init(|| init_devtmpfs(task))
}

fn init_devtmpfs(current_task: &CurrentTask) -> FileSystemHandle {
    let fs = TmpFs::new_fs(current_task.kernel());
    let root = fs.root();

    let mkchr = |name, device_type| {
        root.create_node(current_task, name, mode!(IFCHR, 0o666), device_type, FsCred::root())
            .unwrap();
    };

    let mkdir = |name| {
        root.create_node(current_task, name, mode!(IFDIR, 0o755), DeviceType::NONE, FsCred::root())
            .unwrap();
    };

    mkchr(b"null", DeviceType::NULL);
    mkchr(b"zero", DeviceType::ZERO);
    mkchr(b"full", DeviceType::FULL);
    mkchr(b"random", DeviceType::RANDOM);
    mkchr(b"urandom", DeviceType::URANDOM);
    root.create_symlink(current_task, b"fd", b"/proc/self/fd", FsCred::root()).unwrap();

    mkdir(b"shm");

    // tty related nodes
    mkdir(b"pts");
    mkchr(b"tty", DeviceType::TTY);
    root.create_symlink(current_task, b"ptmx", b"pts/ptmx", FsCred::root()).unwrap();

    mkchr(b"fb0", DeviceType::FB0);
    fs
}
