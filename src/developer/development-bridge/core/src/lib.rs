// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, async_trait::async_trait, fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
};

pub mod args;
pub mod constants;

pub use core_macros::{ffx_command, ffx_plugin};

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum ConfigLevel {
    Defaults,
    Build,
    Global,
    User,
}

#[async_trait]
pub trait RemoteControlProxySource {
    async fn get_remote_proxy(&self) -> Result<RemoteControlProxy, Error>;
}

pub trait DaemonProxySource {
    fn get_daemon_proxy(&self) -> &DaemonProxy;
}
