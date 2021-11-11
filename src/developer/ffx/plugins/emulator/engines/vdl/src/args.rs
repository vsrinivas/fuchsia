// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::{AudioModel, PointingDevice};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, PartialEq, Serialize)]
/// This is a placeholder for VDL-engine-specific parameters. Historically
/// these were held in the StartCommand type, but in the new design we will
/// have engine-specific types that are the inner type of a HostConfig
/// variant. See emulator/config_types for the HostConfig parent. Note that
/// this is distinct from the DeviceSpec and GuestConfig, which contain
/// parameters for the virtual device configuration and the guest operating
/// system configuration respectively.
pub struct VdlConfig {
    pub audio: AudioModel,
    pub pointing_device: PointingDevice,
    pub image_size: String,
    pub ram_mb: usize,
    pub window_height: usize,
    pub window_width: usize,
}

impl VdlConfig {
    pub fn new() -> Self {
        Self {
            audio: AudioModel::None,
            image_size: "2G".to_string(),
            pointing_device: PointingDevice::None,
            ram_mb: 8192,
            window_width: 1280,
            window_height: 800,
        }
    }
}
