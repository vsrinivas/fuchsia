// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice client.

use std::convert::TryInto as _;

use fidl_fuchsia_hardware_network as netdev;

use crate::error::Result;
use crate::session::{Config, DeviceInfo, Session, Task};

/// A client that communicates with a network device to send and receive packets.
pub struct Client {
    device: netdev::DeviceProxy,
}

impl Client {
    /// Create a new network device client for the [`netdev::DeviceProxy`].
    pub fn new(device: netdev::DeviceProxy) -> Self {
        Client { device }
    }

    /// Retrieves information about the underlying network device.
    pub async fn device_info(&self) -> Result<DeviceInfo> {
        Ok(self.device.get_info().await?.try_into()?)
    }

    /// Create a new session with the given the given `name` and `config`.
    pub async fn new_session(&self, name: &str, config: Config) -> Result<(Session, Task)> {
        Session::new(&self.device, name, config).await
    }

    /// Create a primary session.
    pub async fn primary_session(
        &self,
        name: &str,
        buffer_length: usize,
    ) -> Result<(Session, Task)> {
        let device_info = self.device_info().await?;
        let primary_config = device_info.primary_config(buffer_length)?;
        Session::new(&self.device, name, primary_config).await
    }
}
