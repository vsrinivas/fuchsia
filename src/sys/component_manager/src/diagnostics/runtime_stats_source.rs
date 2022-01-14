// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::hooks::RuntimeInfo,
    async_trait::async_trait,
    fidl_fuchsia_diagnostics_types::{ComponentDiagnostics, Task as DiagnosticsTask, TaskUnknown},
    fuchsia_zircon::sys as zx_sys,
    fuchsia_zircon::{self as zx, AsHandleRef, Task},
    futures::channel::oneshot,
};

/// Trait that all structs that behave as Task's implement.
/// Used for simplying testing.
#[async_trait]
pub trait RuntimeStatsSource {
    /// The koid of the Cpu stats source.
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status>;
    /// The returned future will resolve when the task is terminated.
    fn handle_ref(&self) -> zx::HandleRef<'_>;
    /// Provides the runtime info containing the stats.
    async fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status>;
}

/// Trait for the container returned by a `DiagnosticsReceiverProvider`.
/// Used for simplying testing.
#[async_trait]
pub trait RuntimeStatsContainer<T: RuntimeStatsSource> {
    /// The task running a component.
    fn take_component_task(&mut self) -> Option<T>;
    /// An optional parent task running multiple components including `component_task`.
    fn take_parent_task(&mut self) -> Option<T>;
}

/// Trait for the providers of asynchronous receivers where the diagnostics data is sent.
/// Used for simplying testing.
#[async_trait]
pub trait DiagnosticsReceiverProvider<T, S>
where
    T: RuntimeStatsContainer<S>,
    S: RuntimeStatsSource,
{
    /// Fetches a oneshot receiver that will eventually resolve to the diagnostics of a component
    /// if the runner provides them.
    async fn get_receiver(&self) -> Option<oneshot::Receiver<T>>;
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

    fn handle_ref(&self) -> zx::HandleRef<'_> {
        match &self {
            DiagnosticsTask::Job(job) => job.as_handle_ref(),
            DiagnosticsTask::Process(process) => process.as_handle_ref(),
            DiagnosticsTask::Thread(thread) => thread.as_handle_ref(),
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

#[async_trait]
impl RuntimeStatsContainer<DiagnosticsTask> for ComponentDiagnostics {
    fn take_component_task(&mut self) -> Option<DiagnosticsTask> {
        self.tasks.as_mut().and_then(|tasks| tasks.component_task.take())
    }

    fn take_parent_task(&mut self) -> Option<DiagnosticsTask> {
        self.tasks.as_mut().and_then(|tasks| tasks.parent_task.take())
    }
}

#[async_trait]
impl DiagnosticsReceiverProvider<ComponentDiagnostics, DiagnosticsTask> for RuntimeInfo {
    async fn get_receiver(&self) -> Option<oneshot::Receiver<ComponentDiagnostics>> {
        let mut receiver_guard = self.diagnostics_receiver.lock().await;
        receiver_guard.take()
    }
}
