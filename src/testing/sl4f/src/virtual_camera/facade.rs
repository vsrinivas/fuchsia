// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_syslog::macros::*;
use serde_json::{to_value, Value};

/// Facade providing access to SetUi interfaces.
#[derive(Debug)]
pub struct VirtualCameraFacade {}

impl VirtualCameraFacade {
    pub fn new() -> VirtualCameraFacade {
        VirtualCameraFacade {}
    }

    pub async fn add_stream_config(&self, _args: Value) -> Result<Value, Error> {
        fx_log_info!("VirtualCameraFacade.add_stream_config unimplemented. see b/195761854");
        Ok(to_value(true)?)
    }

    pub async fn add_to_device_watcher(&self) -> Result<Value, Error> {
        fx_log_info!("VirtualCameraFacade.add_to_device_watcher unimplemented. see b/195761854");
        Ok(to_value(true)?)
    }

    // TODO(b/195762320) Add remaining method parsing for AddDataSource,
    // SetDataSourceForStreamConfig, ClearDataSourceForStreamConfig
}
