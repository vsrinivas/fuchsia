// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::task::Kernel;
use crate::types::{statfs, Errno, NAME_MAX, SOCKFS_MAGIC};

/// `SocketFs` is the file system where anonymous socket nodes are created, for example in
/// `sys_socket`.
pub struct SocketFs {}
impl FileSystemOps for SocketFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        let mut stat = statfs::default();
        stat.f_type = SOCKFS_MAGIC as i64;
        stat.f_bsize = *PAGE_SIZE;
        stat.f_namelen = NAME_MAX as i64;
        Ok(stat)
    }
}

/// Returns a handle to the `SocketFs` instance in `kernel`, initializing it if needed.
pub fn socket_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.socket_fs.get_or_init(|| FileSystem::new(SocketFs {}))
}
