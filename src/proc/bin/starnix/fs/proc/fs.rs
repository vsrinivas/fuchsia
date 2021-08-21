// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::proc_directory::*;
use crate::fs::*;
use crate::task::*;

use std::sync::Arc;
use std::sync::Weak;

/// Returns `kernel`'s procfs instance, initializing it if needed.
pub fn proc_fs(kernel: Arc<Kernel>) -> FileSystemHandle {
    kernel.proc_fs.get_or_init(|| ProcFs::new(Arc::downgrade(&kernel))).clone()
}

/// `ProcFs` is a filesystem that exposes runtime information about a `Kernel` instance.
pub struct ProcFs {}

impl FileSystemOps for Arc<ProcFs> {}

impl ProcFs {
    /// Creates a new instance of `ProcFs` for the given `kernel`.
    pub fn new(kernel: Weak<Kernel>) -> FileSystemHandle {
        let procfs = Arc::new(ProcFs {});
        FileSystem::new_with_root(procfs, ProcDirectory::new(kernel))
    }
}
