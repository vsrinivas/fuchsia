// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use core_macros::{ffx_command, ffx_plugin};

use {
    anyhow::Result,
    async_trait::async_trait,
    ffx_writer::Writer,
    fidl_fuchsia_developer_ffx::{DaemonProxy, FastbootProxy, TargetProxy, VersionInfo},
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
