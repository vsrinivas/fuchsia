// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::virtual_camera::{facade::VirtualCameraFacade, types::VirtualCameraMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

#[async_trait(?Send)]
impl Facade for VirtualCameraFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            VirtualCameraMethod::AddStreamConfig => self.add_stream_config(args).await,
            VirtualCameraMethod::AddToDeviceWatcher => self.add_to_device_watcher().await,
            // TODO(b/195762320) Add remaining method parsing for AddDataSource,
            // SetDataSourceForStreamConfig, ClearDataSourceForStreamConfig
        }
    }
}
