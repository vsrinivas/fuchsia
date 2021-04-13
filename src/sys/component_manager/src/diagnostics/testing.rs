// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    crate::diagnostics::runtime_stats_source::RuntimeStatsSource,
    async_trait::async_trait,
    fuchsia_zircon as zx, fuchsia_zircon_sys as zx_sys,
    futures::lock::Mutex,
    std::{collections::VecDeque, sync::Arc},
};

#[derive(Default)]
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
