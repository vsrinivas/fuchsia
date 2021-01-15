// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct DeviceInfo {
    pub build_tag: String,
}

impl DeviceInfo {
    pub const fn new(build_tag: String) -> DeviceInfo {
        DeviceInfo { build_tag }
    }
}
