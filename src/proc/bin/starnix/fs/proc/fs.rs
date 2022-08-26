// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::proc_directory::*;
use crate::fs::*;
use crate::task::*;

use std::sync::Arc;

/// Returns `kernel`'s procfs instance, initializing it if needed.
pub fn proc_fs(kernel: Arc<Kernel>) -> FileSystemHandle {
    kernel.proc_fs.get_or_init(|| ProcFs::new_fs(&kernel)).clone()
}

/// `ProcFs` is a filesystem that exposes runtime information about a `Kernel` instance.
pub struct ProcFs;
impl FileSystemOps for Arc<ProcFs> {}

impl ProcFs {
    /// Creates a new instance of `ProcFs` for the given `kernel`.
    pub fn new_fs(kernel: &Arc<Kernel>) -> FileSystemHandle {
        let fs = FileSystem::new(kernel, Arc::new(ProcFs));
        fs.set_root(ProcDirectory::new(&fs, Arc::downgrade(kernel)));
        fs
    }
}
