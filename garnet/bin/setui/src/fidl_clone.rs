// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_setui::*;

/// A placeholder for real cloning support in FIDL generated Rust code.
/// TODO(QA-715): Remove
pub trait FIDLClone {
    fn clone(&self) -> Self;
}

impl FIDLClone for SettingData {
    fn clone(&self) -> SettingData {
        match self {
            SettingData::StringValue(val) => {
                return SettingData::StringValue(val.to_string());
            }
            SettingData::TimeZoneValue(val) => {
                return SettingData::TimeZoneValue(val.clone());
            }
            SettingData::Connectivity(val) => {
                return SettingData::Connectivity(val.clone());
            }
            SettingData::Intl(val) => {
                return SettingData::Intl(val.clone());
            }
            SettingData::Wireless(val) => {
                return SettingData::Wireless(val.clone());
            }
            SettingData::Account(val) => {
                return SettingData::Account(val.clone());
            }
        }
    }
}

impl FIDLClone for AccountSettings {
    fn clone(&self) -> Self {
        return AccountSettings { mode: self.mode };
    }
}

impl<T: FIDLClone> FIDLClone for Vec<T> {
    fn clone(&self) -> Self {
        return self.into_iter().map(FIDLClone::clone).collect();
    }
}

impl<T: FIDLClone> FIDLClone for Option<Box<T>> {
    fn clone(&self) -> Self {
        match self {
            None => None,
            Some(val) => Some(Box::new(val.as_ref().clone())),
        }
    }
}

impl FIDLClone for TimeZone {
    fn clone(&self) -> Self {
        return TimeZone {
            id: self.id.clone(),
            name: self.name.clone(),
            region: self.region.clone(),
        };
    }
}
