// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::mm::PAGE_SIZE;
use crate::mode;
use crate::task::Kernel;
use crate::types::*;

use parking_lot::Mutex;
use std::sync::Arc;

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

/// Creates a `FileHandle` where the associated `FsNode` contains a socket.
///
/// # Parameters
/// - `kernel`: The kernel that is used to fetch `SocketFs`, to store the created socket node.
/// - `open_flags`: The `OpenFlags` which are used to create the `FileObject`.
pub fn new_socket(kernel: &Kernel, open_flags: OpenFlags) -> Result<FileHandle, Errno> {
    let fs = socket_fs(kernel);
    let mode = mode!(IFSOCK, 0o777);
    let node = fs.create_node(Box::new(SpecialNode), mode);
    node.set_socket(Socket::new())?;

    Ok(FileObject::new_anonymous(Box::new(NullFile), Arc::clone(&node), open_flags))
}

pub struct Socket {}

/// A `SocketHandle` is a `Socket` wrapped in a `Arc<Mutex<..>>`. This is used to share sockets
/// between file nodes.
pub type SocketHandle = Arc<Mutex<Socket>>;

impl Socket {
    pub fn new() -> SocketHandle {
        Arc::new(Mutex::new(Socket {}))
    }
}
