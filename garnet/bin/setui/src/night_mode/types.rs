// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub struct NightModeInfo {
    pub night_mode_enabled: Option<bool>,
}

impl NightModeInfo {
    pub(super) const fn empty() -> NightModeInfo {
        NightModeInfo { night_mode_enabled: None }
    }
}
