// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_power::{BatteryInfo, TimeRemaining};

/// Manual implementations of `Clone` for BatteryInfo & TimeRemaining FIDL data types.
pub trait CloneExt {
    fn clone(&self) -> Self;
}

// This manual impl is necessary because `BatteryInfo` is a table
// without [MaxHandles], so it could potentially have non-cloneable
// handles added to it in the future.
impl CloneExt for BatteryInfo {
    fn clone(&self) -> Self {
        BatteryInfo {
            status: self.status.clone(),
            charge_status: self.charge_status.clone(),
            charge_source: self.charge_source.clone(),
            level_percent: self.level_percent.clone(),
            level_status: self.level_status.clone(),
            health: self.health.clone(),
            time_remaining: match &self.time_remaining {
                Some(t) => Some(t.clone()),
                _ => Some(TimeRemaining::Indeterminate(0)),
            },
            timestamp: self.timestamp.clone(),
        }
    }
}

// This manual impl is necessary because `TimeRemaining` is an xunion
// without [MaxHandles], so it could potentially have non-cloneable
// handles added to it in the future.
impl CloneExt for TimeRemaining {
    fn clone(&self) -> Self {
        match self {
            Self::FullCharge(c) => Self::FullCharge(*c),
            Self::BatteryLife(l) => Self::BatteryLife(*l),
            _ => Self::Indeterminate(0),
        }
    }
}
