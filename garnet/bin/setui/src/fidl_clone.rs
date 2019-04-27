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

impl FIDLClone for IntlSettings {
    fn clone(&self) -> Self {
        return IntlSettings {
            locales: self.locales.clone(),
            hour_cycle: self.hour_cycle,
            temperature_unit: self.temperature_unit,
        };
    }
}

impl FIDLClone for ConnectedState {
    fn clone(&self) -> ConnectedState {
        return ConnectedState { reachability: self.reachability };
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

impl FIDLClone for TimeZoneInfo {
    fn clone(&self) -> TimeZoneInfo {
        return TimeZoneInfo { current: self.current.clone(), available: self.available.clone() };
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

impl FIDLClone for WirelessState {
    fn clone(&self) -> Self {
        return WirelessState { wireless_networks: self.wireless_networks.clone() };
    }
}

impl FIDLClone for WirelessNetwork {
    fn clone(&self) -> Self {
        return WirelessNetwork {
            internal_id: self.internal_id,
            ssid: self.ssid.clone(),
            wpa_auth: self.wpa_auth,
            wpa_cipher: self.wpa_cipher,
            access_points: self.access_points.clone(),
        };
    }
}

impl FIDLClone for WirelessAccessPoint {
    fn clone(&self) -> Self {
        return WirelessAccessPoint {
            bssid: self.bssid.clone(),
            frequency: self.frequency,
            rssi: self.rssi,
            status: self.status,
        };
    }
}
