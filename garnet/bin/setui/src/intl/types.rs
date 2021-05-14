// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::IntlSettings;
use serde::{Deserialize, Serialize};

use crate::base::Merge;

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct IntlInfo {
    pub locales: Option<Vec<LocaleId>>,
    pub temperature_unit: Option<TemperatureUnit>,
    pub time_zone_id: Option<String>,
    pub hour_cycle: Option<HourCycle>,
}

impl Merge for IntlInfo {
    fn merge(&self, other: Self) -> Self {
        IntlInfo {
            locales: other.locales.or_else(|| self.locales.clone()),
            temperature_unit: other.temperature_unit.or(self.temperature_unit),
            time_zone_id: other.time_zone_id.or_else(|| self.time_zone_id.clone()),
            hour_cycle: other.hour_cycle.or(self.hour_cycle),
        }
    }
}

impl From<fidl_fuchsia_settings::IntlSettings> for IntlInfo {
    fn from(src: IntlSettings) -> Self {
        IntlInfo {
            locales: src.locales.map(|locales| {
                locales.into_iter().map(fidl_fuchsia_intl::LocaleId::into).collect()
            }),
            temperature_unit: src.temperature_unit.map(fidl_fuchsia_intl::TemperatureUnit::into),
            time_zone_id: src.time_zone_id.map(|tz| tz.id),
            hour_cycle: src.hour_cycle.map(fidl_fuchsia_settings::HourCycle::into),
        }
    }
}

impl From<IntlInfo> for fidl_fuchsia_settings::IntlSettings {
    fn from(info: IntlInfo) -> IntlSettings {
        let mut intl_settings = IntlSettings::EMPTY;

        intl_settings.locales =
            info.locales.map(|locales| locales.into_iter().map(LocaleId::into).collect());
        intl_settings.temperature_unit = info.temperature_unit.map(TemperatureUnit::into);
        intl_settings.time_zone_id =
            info.time_zone_id.map(|tz| fidl_fuchsia_intl::TimeZoneId { id: tz });
        intl_settings.hour_cycle = info.hour_cycle.map(HourCycle::into);

        intl_settings
    }
}

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct LocaleId {
    pub id: String,
}

impl From<fidl_fuchsia_intl::LocaleId> for LocaleId {
    fn from(src: fidl_fuchsia_intl::LocaleId) -> Self {
        LocaleId { id: src.id }
    }
}

impl From<LocaleId> for fidl_fuchsia_intl::LocaleId {
    fn from(src: LocaleId) -> Self {
        fidl_fuchsia_intl::LocaleId { id: src.id }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum TemperatureUnit {
    Celsius,
    Fahrenheit,
}

impl From<fidl_fuchsia_intl::TemperatureUnit> for TemperatureUnit {
    fn from(src: fidl_fuchsia_intl::TemperatureUnit) -> Self {
        match src {
            fidl_fuchsia_intl::TemperatureUnit::Celsius => TemperatureUnit::Celsius,
            fidl_fuchsia_intl::TemperatureUnit::Fahrenheit => TemperatureUnit::Fahrenheit,
        }
    }
}

impl From<TemperatureUnit> for fidl_fuchsia_intl::TemperatureUnit {
    fn from(src: TemperatureUnit) -> Self {
        match src {
            TemperatureUnit::Celsius => fidl_fuchsia_intl::TemperatureUnit::Celsius,
            TemperatureUnit::Fahrenheit => fidl_fuchsia_intl::TemperatureUnit::Fahrenheit,
        }
    }
}

#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum HourCycle {
    Unknown,
    H11,
    H12,
    H23,
    H24,
}

impl From<fidl_fuchsia_settings::HourCycle> for HourCycle {
    fn from(src: fidl_fuchsia_settings::HourCycle) -> Self {
        match src {
            fidl_fuchsia_settings::HourCycle::Unknown => HourCycle::Unknown,
            fidl_fuchsia_settings::HourCycle::H11 => HourCycle::H11,
            fidl_fuchsia_settings::HourCycle::H12 => HourCycle::H12,
            fidl_fuchsia_settings::HourCycle::H23 => HourCycle::H23,
            fidl_fuchsia_settings::HourCycle::H24 => HourCycle::H24,
        }
    }
}

impl From<HourCycle> for fidl_fuchsia_settings::HourCycle {
    fn from(src: HourCycle) -> Self {
        match src {
            HourCycle::Unknown => fidl_fuchsia_settings::HourCycle::Unknown,
            HourCycle::H11 => fidl_fuchsia_settings::HourCycle::H11,
            HourCycle::H12 => fidl_fuchsia_settings::HourCycle::H12,
            HourCycle::H23 => fidl_fuchsia_settings::HourCycle::H23,
            HourCycle::H24 => fidl_fuchsia_settings::HourCycle::H24,
        }
    }
}
