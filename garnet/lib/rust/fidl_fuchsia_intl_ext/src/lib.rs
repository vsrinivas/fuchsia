// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_intl::{CalendarId, LocaleId, Profile, TimeZoneId};

/// Manual implementations of `Clone` for intl FIDL data types.
pub trait CloneExt {
    fn clone(&self) -> Self;
}

impl CloneExt for Profile {
    fn clone(&self) -> Self {
        Profile {
            locales: self.locales.clone(),
            calendars: self.calendars.clone(),
            time_zones: self.time_zones.clone(),
            temperature_unit: self.temperature_unit.clone(),
        }
    }
}

impl<T: CloneExt> CloneExt for Option<Vec<T>> {
    fn clone(&self) -> Self {
        self.as_ref().map(|v| v.iter().map(CloneExt::clone).collect())
    }
}

impl CloneExt for LocaleId {
    fn clone(&self) -> Self {
        LocaleId { id: self.id.to_string() }
    }
}

impl CloneExt for CalendarId {
    fn clone(&self) -> Self {
        CalendarId { id: self.id.to_string() }
    }
}

impl CloneExt for TimeZoneId {
    fn clone(&self) -> Self {
        TimeZoneId { id: self.id.to_string() }
    }
}
