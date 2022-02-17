// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use core_macros::{ffx_command, ffx_plugin};

use {
    anyhow::Result,
    async_trait::async_trait,
    ffx_writer::Writer,
    fidl_fuchsia_developer_bridge::{DaemonProxy, FastbootProxy, TargetProxy, VersionInfo},
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
};

#[async_trait(?Send)]
pub trait Injector {
    async fn daemon_factory(&self) -> Result<DaemonProxy>;
    async fn remote_factory(&self) -> Result<RemoteControlProxy>;
    async fn fastboot_factory(&self) -> Result<FastbootProxy>;
    async fn target_factory(&self) -> Result<TargetProxy>;
    async fn is_experiment(&self, key: &str) -> bool;
    async fn build_info(&self) -> Result<VersionInfo>;
    async fn writer(&self) -> Result<Writer>;
}

pub struct PluginResult(Result<i32>);

impl From<Result<()>> for PluginResult {
    fn from(res: Result<()>) -> Self {
        PluginResult(res.map(|_| 0))
    }
}

impl From<Result<i32>> for PluginResult {
    fn from(res: Result<i32>) -> Self {
        PluginResult(res)
    }
}

impl From<PluginResult> for Result<i32> {
    fn from(res: PluginResult) -> Self {
        res.0
    }
}

impl From<PluginResult> for Result<()> {
    fn from(_res: PluginResult) -> Self {
        Ok(())
    }
}
