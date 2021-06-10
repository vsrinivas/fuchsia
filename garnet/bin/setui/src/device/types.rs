// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub build_tag: String,
}

impl DeviceInfo {
    #[cfg(test)]
    pub(crate) const fn new(build_tag: String) -> DeviceInfo {
        DeviceInfo { build_tag }
    }
}

impl From<DeviceInfo> for SettingInfo {
    fn from(info: DeviceInfo) -> SettingInfo {
        SettingInfo::Device(info)
    }
}
