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
