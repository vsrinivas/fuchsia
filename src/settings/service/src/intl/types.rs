// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::IntlSettings;
use serde::{Deserialize, Serialize};

use crate::base::Merge;
use settings_storage::fidl_storage::FidlStorageConvertible;

#[derive(PartialEq, Debug, Clone, Serialize, Deserialize)]
pub struct IntlInfo {
    pub locales: Option<Vec<LocaleId>>,
    pub temperature_unit: Option<TemperatureUnit>,
    pub time_zone_id: Option<String>,
    pub hour_cycle: Option<HourCycle>,
}

impl FidlStorageConvertible for IntlInfo {
    type Storable = fidl_fuchsia_settings::IntlSettings;
    const KEY: &'static str = "intl";

    fn default_value() -> Self {
        IntlInfo {
            // `-x-fxdef` is a private use extension and a special marker denoting that the
            // setting is a fallback default, and not actually set through any user action.
            locales: Some(vec![LocaleId { id: "en-US-x-fxdef".to_string() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
            time_zone_id: Some("UTC".to_string()),
            hour_cycle: Some(HourCycle::H12),
        }
    }

    fn to_storable(self) -> Self::Storable {
        self.into()
    }

    fn from_storable(storable: Self::Storable) -> Self {
        storable.into()
    }
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

#[cfg(test)]
mod tests {
    use crate::intl::types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};
    use fidl_fuchsia_settings::IntlSettings;
    use settings_storage::fidl_storage::FidlStorageConvertible;

    const TIME_ZONE_ID: &str = "PDT";
    const LOCALE_ID: &str = "en_us";

    #[test]
    fn fidl_storage_convertible_from_storable_empty() {
        let info = IntlInfo::from_storable(IntlSettings::EMPTY);

        assert_eq!(
            info,
            IntlInfo {
                locales: None,
                temperature_unit: None,
                time_zone_id: None,
                hour_cycle: None,
            }
        );
    }

    #[test]
    fn fidl_storage_convertible_from_storable() {
        let intl_settings = IntlSettings {
            locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: LOCALE_ID.into() }]),
            temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
            time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: TIME_ZONE_ID.to_string() }),
            hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H12),
            ..IntlSettings::EMPTY
        };

        let info = IntlInfo::from_storable(intl_settings);

        assert_eq!(
            info,
            IntlInfo {
                locales: Some(vec![LocaleId { id: LOCALE_ID.into() }]),
                temperature_unit: Some(TemperatureUnit::Celsius),
                time_zone_id: Some(TIME_ZONE_ID.to_string()),
                hour_cycle: Some(HourCycle::H12),
            }
        );
    }

    #[test]
    fn fidl_storage_convertible_to_storable_empty() {
        let info = IntlInfo {
            locales: None,
            temperature_unit: None,
            time_zone_id: None,
            hour_cycle: None,
        };
        let storable = info.to_storable();

        assert_eq!(storable, IntlSettings::EMPTY,);
    }

    #[test]
    fn fidl_storage_convertible_to_storable() {
        let info = IntlInfo {
            locales: Some(vec![LocaleId { id: LOCALE_ID.into() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
            time_zone_id: Some(TIME_ZONE_ID.to_string()),
            hour_cycle: Some(HourCycle::H12),
        };

        let storable = info.to_storable();

        assert_eq!(
            storable,
            IntlSettings {
                locales: Some(vec![fidl_fuchsia_intl::LocaleId { id: LOCALE_ID.into() }]),
                temperature_unit: Some(fidl_fuchsia_intl::TemperatureUnit::Celsius),
                time_zone_id: Some(fidl_fuchsia_intl::TimeZoneId { id: TIME_ZONE_ID.to_string() }),
                hour_cycle: Some(fidl_fuchsia_settings::HourCycle::H12),
                ..IntlSettings::EMPTY
            }
        );
    }
}
