// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::diagnostics::runtime_stats_source::*,
    async_trait::async_trait,
    fuchsia_zircon as zx, fuchsia_zircon_sys as zx_sys,
    futures::{channel::oneshot, lock::Mutex},
    std::{collections::VecDeque, sync::Arc},
};

/// Mock for a Task. Holds a queue of runtime infos (measurements) that will be fetched for test
/// purposes.
#[derive(Clone, Debug, Default)]
pub struct FakeTask {
    values: Arc<Mutex<VecDeque<zx::TaskRuntimeInfo>>>,
    koid: zx_sys::zx_koid_t,
    pub invalid_handle: bool,
}

impl FakeTask {
    pub fn new(koid: zx_sys::zx_koid_t, values: Vec<zx::TaskRuntimeInfo>) -> Self {
        Self { koid, invalid_handle: false, values: Arc::new(Mutex::new(values.into())) }
    }
}

#[async_trait]
impl RuntimeStatsSource for FakeTask {
    fn koid(&self) -> Result<zx_sys::zx_koid_t, zx::Status> {
        Ok(self.koid.clone())
    }
    fn handle_is_invalid(&self) -> bool {
        self.invalid_handle
    }
    async fn get_runtime_info(&self) -> Result<zx::TaskRuntimeInfo, zx::Status> {
        Ok(self.values.lock().await.pop_front().unwrap_or(zx::TaskRuntimeInfo::default()))
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
