// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::IntlSettings;
use serde_derive::{Deserialize, Serialize};

use crate::switchboard::base::Merge;

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct IntlInfo {
    pub locales: Option<Vec<LocaleId>>,
    pub temperature_unit: Option<TemperatureUnit>,
    pub time_zone_id: Option<String>,
}

impl Merge for IntlInfo {
    fn merge(&self, other: Self) -> Self {
        IntlInfo {
            locales: self.locales.clone().or(other.locales),
            temperature_unit: self.temperature_unit.or(other.temperature_unit),
            time_zone_id: self.time_zone_id.clone().or(other.time_zone_id),
        }
    }
}

impl From<fidl_fuchsia_settings::IntlSettings> for IntlInfo {
    fn from(src: IntlSettings) -> Self {
        IntlInfo {
            locales: src.locales.map_or(None, |locales| {
                Some(locales.into_iter().map(fidl_fuchsia_intl::LocaleId::into).collect())
            }),
            temperature_unit: src.temperature_unit.map(fidl_fuchsia_intl::TemperatureUnit::into),
            time_zone_id: src.time_zone_id.map_or(None, |tz| Some(tz.id)),
        }
    }
}

impl Into<fidl_fuchsia_settings::IntlSettings> for IntlInfo {
    fn into(self) -> IntlSettings {
        let mut intl_settings = IntlSettings::empty();

        intl_settings.locales = self
            .locales
            .map_or(None, |locales| Some(locales.into_iter().map(LocaleId::into).collect()));
        intl_settings.temperature_unit = self.temperature_unit.map(TemperatureUnit::into);
        intl_settings.time_zone_id =
            self.time_zone_id.map_or(None, |tz| Some(fidl_fuchsia_intl::TimeZoneId { id: tz }));

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
