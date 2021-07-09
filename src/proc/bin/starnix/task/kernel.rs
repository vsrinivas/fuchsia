// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef};
use parking_lot::RwLock;
use std::ffi::CString;
use std::sync::Arc;

#[cfg(test)]
use fuchsia_zircon::HandleBased;

use crate::devices::DeviceRegistry;
use crate::task::*;

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,

    /// The scheduler associated with this kernel. The scheduler stores state like suspended tasks,
    /// pending signals, etc.
    pub scheduler: RwLock<Scheduler>,

    /// The devices that exist in this kernel.
    pub devices: DeviceRegistry,
}

impl Kernel {
    pub fn new(name: &CString) -> Result<Arc<Kernel>, zx::Status> {
        let job = fuchsia_runtime::job_default().create_child_job()?;
        job.set_name(&name)?;
        let kernel = Kernel {
            job,
            pids: RwLock::new(PidTable::new()),
            scheduler: RwLock::new(Scheduler::new()),
            devices: DeviceRegistry::new(),
        };
        Ok(Arc::new(kernel))
    }

    #[cfg(test)]
    pub fn new_for_testing() -> Arc<Kernel> {
        Arc::new(Kernel {
            job: zx::Job::from_handle(zx::Handle::invalid()),
            pids: RwLock::new(PidTable::new()),
            scheduler: RwLock::new(Scheduler::new()),
            devices: DeviceRegistry::new(),
        })
    }
}
