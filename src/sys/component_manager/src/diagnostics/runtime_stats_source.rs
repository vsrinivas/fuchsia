// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_diagnostics_types::{Task as DiagnosticsTask, TaskUnknown},
    fuchsia_zircon::{self as zx, AsHandleRef, Task},
    fuchsia_zircon_sys as zx_sys,
};

#[async_trait]
pub trait RuntimeStatsSource {
    /// The koid of the Cpu stats source.
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status>;
    /// Whether the handle backing up this source is invalid.
    fn handle_is_invalid(&self) -> bool;
    /// Provides the runtime info containing the stats.
    async fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status>;
}

#[async_trait]
impl RuntimeStatsSource for DiagnosticsTask {
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status> {
        let info = match &self {
            DiagnosticsTask::Job(job) => job.basic_info(),
            DiagnosticsTask::Process(process) => process.basic_info(),
            DiagnosticsTask::Thread(thread) => thread.basic_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }?;
        Ok(info.koid.raw_koid())
    }

    fn handle_is_invalid(&self) -> bool {
        match &self {
            DiagnosticsTask::Job(job) => job.as_handle_ref().is_invalid(),
            DiagnosticsTask::Process(process) => process.as_handle_ref().is_invalid(),
            DiagnosticsTask::Thread(thread) => thread.as_handle_ref().is_invalid(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }
    }

    async fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
        match &self {
            DiagnosticsTask::Job(job) => job.get_runtime_info(),
            DiagnosticsTask::Process(process) => process.get_runtime_info(),
            DiagnosticsTask::Thread(thread) => thread.get_runtime_info(),
            TaskUnknown!() => {
                unreachable!("only jobs, threads and processes are tasks");
            }
        }
    }
}
