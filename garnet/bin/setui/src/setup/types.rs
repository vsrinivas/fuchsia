// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Copy, Deserialize, Serialize)]
pub struct SetupInfo {
    pub configuration_interfaces: ConfigurationInterfaceFlags,
}

bitflags! {
    #[derive(Serialize, Deserialize)]
    pub struct ConfigurationInterfaceFlags: u32 {
        const ETHERNET = 1 << 0;
        const WIFI = 1 << 1;
        const DEFAULT = Self::WIFI.bits;
    }
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct SetConfigurationInterfacesParams {
    pub config_interfaces_flags: ConfigurationInterfaceFlags,
    // Set true to reboot the device, otherwise, set it as false.
    pub should_reboot: bool,
}
