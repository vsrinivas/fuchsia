// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_intl::Profile;

/// Manual implementations of `Clone` for intl FIDL data types.
pub trait CloneExt {
    fn clone(&self) -> Self;
}

// This manual impl is necessary because `Profile` is a table without
// [MaxHandles], so it could potentially have non-cloneable handles
// added to it in the future.
impl CloneExt for Profile {
    fn clone(&self) -> Self {
        Profile {
            locales: self.locales.clone(),
            calendars: self.calendars.clone(),
            time_zones: self.time_zones.clone(),
            temperature_unit: self.temperature_unit.clone(),
            ..Profile::empty()
        }
    }
}
