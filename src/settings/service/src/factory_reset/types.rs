// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(Debug, Copy, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FactoryResetInfo {
    pub is_local_reset_allowed: bool,
}

impl FactoryResetInfo {
    pub const fn new(is_local_reset_allowed: bool) -> Self {
        Self { is_local_reset_allowed }
    }
}

impl From<FactoryResetInfo> for fidl_fuchsia_settings::FactoryResetSettings {
    fn from(info: FactoryResetInfo) -> Self {
        fidl_fuchsia_settings::FactoryResetSettings {
            is_local_reset_allowed: Some(info.is_local_reset_allowed),
            ..fidl_fuchsia_settings::FactoryResetSettings::EMPTY
        }
    }
}

impl From<fidl_fuchsia_settings::FactoryResetSettings> for FactoryResetInfo {
    fn from(settings: fidl_fuchsia_settings::FactoryResetSettings) -> Self {
        let is_local_reset_allowed = settings.is_local_reset_allowed.unwrap_or(true);
        FactoryResetInfo::new(is_local_reset_allowed)
    }
}
