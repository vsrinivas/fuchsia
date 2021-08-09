// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported virtual camera device commands.
pub enum VirtualCameraMethod {
    AddStreamConfig,
    AddToDeviceWatcher,
}

impl std::str::FromStr for VirtualCameraMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "AddStreamConfig" => Ok(VirtualCameraMethod::AddStreamConfig),
            "AddToDeviceWatcher" => Ok(VirtualCameraMethod::AddToDeviceWatcher),
            _ => {
                return Err(format_err!("invalid Virtual Camera Device Facade method: {}", method))
            } // TODO(b/195762320) Add remaining method matching for AddDataSource,
              // SetDataSourceForStreamConfig, ClearDataSourceForStreamConfig
        }
    }
}
