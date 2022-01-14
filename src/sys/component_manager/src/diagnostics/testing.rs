// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::diagnostics::{
        runtime_stats_source::*,
        task_info::{TaskInfo, TaskState},
    },
    async_trait::async_trait,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon::sys as zx_sys,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{channel::oneshot, lock::Mutex},
    injectable_time::TimeSource,
    std::{collections::VecDeque, sync::Arc},
};

/// Mock for a Task. Holds a queue of runtime infos (measurements) that will be fetched for test
/// purposes.
#[derive(Clone, Debug)]
pub struct FakeTask {
    values: Arc<Mutex<VecDeque<zx::TaskRuntimeInfo>>>,
    koid: zx_sys::zx_koid_t,
    event: Arc<zx::Event>,
}

impl Default for FakeTask {
    fn default() -> Self {
        Self::new(0, vec![])
    }
}

impl FakeTask {
    pub fn new(koid: zx_sys::zx_koid_t, values: Vec<zx::TaskRuntimeInfo>) -> Self {
        Self {
            koid,
            values: Arc::new(Mutex::new(values.into())),
            event: Arc::new(zx::Event::create().unwrap()),
        }
    }

    pub async fn terminate(&self) {
        self.event
            .signal_handle(zx::Signals::NONE, zx::Signals::TASK_TERMINATED)
            .expect("signal task terminated");
    }
}

#[async_trait]
impl RuntimeStatsSource for FakeTask {
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status> {
        Ok(self.koid.clone())
    }

    fn handle_ref(&self) -> zx::HandleRef<'_> {
        self.event.as_handle_ref()
    }

    async fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
        Ok(self.values.lock().await.pop_front().unwrap_or(zx::TaskRuntimeInfo::default()))
    }
}

impl<U: TimeSource + Send> TaskInfo<FakeTask, U> {
    pub async fn force_terminate(&mut self) {
        match &*self.task.lock().await {
            TaskState::Alive(t) | TaskState::Terminated(t) => t.terminate().await,
            TaskState::TerminatedAndMeasured => {}
        }

        // Since the terminate is done asynchronously, ensure we actually have marked this task as
        // terminated to avoid flaking.
        loop {
            if matches!(
                *self.task.lock().await,
                TaskState::Terminated(_) | TaskState::TerminatedAndMeasured
            ) {
                return;
            }
            fasync::Timer::new(zx::Duration::from_millis(100).after_now()).await;
        }
    }
}

/// Mock for the `RuntimeInfo` object that is provided through the Started hook.
pub struct FakeRuntime {
    container: Mutex<Option<FakeDiagnosticsContainer>>,
}

impl FakeRuntime {
    pub fn new(container: FakeDiagnosticsContainer) -> Self {
        Self { container: Mutex::new(Some(container)) }
    }
}

#[async_trait]
impl DiagnosticsReceiverProvider<FakeDiagnosticsContainer, FakeTask> for FakeRuntime {
    async fn get_receiver(&self) -> Option<oneshot::Receiver<FakeDiagnosticsContainer>> {
        match self.container.lock().await.take() {
            None => None,
            Some(container) => {
                let (snd, rcv) = oneshot::channel();
                snd.send(container).unwrap();
                Some(rcv)
            }
        }
    }
}

/// Mock for the `ComponentDiagnostics` object coming from the runner containing the optional
/// parent task and the component task.
#[derive(Debug)]
pub struct FakeDiagnosticsContainer {
    parent_task: Option<FakeTask>,
    component_task: Option<FakeTask>,
}

impl FakeDiagnosticsContainer {
    pub fn new(component_task: FakeTask, parent_task: Option<FakeTask>) -> Self {
        Self { component_task: Some(component_task), parent_task }
    }
}

#[async_trait]
impl RuntimeStatsContainer<FakeTask> for FakeDiagnosticsContainer {
    fn take_component_task(&mut self) -> Option<FakeTask> {
        self.component_task.take()
    }

    fn take_parent_task(&mut self) -> Option<FakeTask> {
        self.parent_task.take()
    }
}
