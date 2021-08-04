// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};
use once_cell::sync::OnceCell;
use parking_lot::RwLock;
use std::ffi::CString;
use std::sync::Arc;

use crate::fs::FileSystemHandle;
use crate::task::*;

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,

    /// The scheduler associated with this kernel. The scheduler stores state like suspended tasks,
    /// pending signals, etc.
    pub scheduler: RwLock<Scheduler>,

    // Owned by anon_node.rs
    pub anon_fs: OnceCell<FileSystemHandle>,
    // Owned by pipe.rs
    pub pipe_fs: OnceCell<FileSystemHandle>,
}

impl Kernel {
    fn new_empty() -> Kernel {
        Kernel {
            job: zx::Job::from_handle(zx::Handle::invalid()),
            pids: RwLock::new(PidTable::new()),
            scheduler: RwLock::new(Scheduler::new()),
            anon_fs: OnceCell::new(),
            pipe_fs: OnceCell::new(),
        }
    }

    pub fn new(name: &CString) -> Result<Arc<Kernel>, zx::Status> {
        let mut kernel = Self::new_empty();
        kernel.job = fuchsia_runtime::job_default().create_child_job()?;
        kernel.job.set_name(&name)?;
        Ok(Arc::new(kernel))
    }

    #[cfg(test)]
    pub fn new_for_testing() -> Arc<Kernel> {
        Arc::new(Self::new_empty())
    }
}
