// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use fidl_fuchsia_settings::IntlSettings;
use serde::{Deserialize, Serialize};

/// Supported setUi commands.
pub enum SetUiMethod {
    SetNetwork,
    GetNetwork,
    SetIntl,
    GetIntl,
}

impl std::str::FromStr for SetUiMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "SetNetwork" => Ok(SetUiMethod::SetNetwork),
            "GetNetwork" => Ok(SetUiMethod::GetNetwork),
            "SetIntl" => Ok(SetUiMethod::SetIntl),
            "GetIntl" => Ok(SetUiMethod::GetIntl),
            _ => return Err(format_err!("invalid SetUi SL4F method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum SetUiResult {
    Success,
}

#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum NetworkType {
    Ethernet,
    Wifi,
    Unknown,
}

/// --------- Data types for Intl ---------

/// Types used for fuchsia.intl FIDL calls.
/// Implementation is copied from /garnet/bin/setui/src/switchboard/intl_types.rs
/// TODO(fxbug.dev/53501) Remove this once intl_types is defined in a rust_library.
#[derive(PartialEq, Eq, Debug, Clone, Serialize, Deserialize)]
pub struct IntlInfo {
    pub locales: Option<Vec<LocaleId>>,
    pub temperature_unit: Option<TemperatureUnit>,
    pub time_zone_id: Option<String>,
    pub hour_cycle: Option<HourCycle>,
}

/// Conversion from IntlSettings to IntlInfo
impl From<fidl_fuchsia_settings::IntlSettings> for IntlInfo {
    fn from(src: IntlSettings) -> Self {
        IntlInfo {
            locales: src.locales.map_or(None, |locales| {
                Some(locales.into_iter().map(fidl_fuchsia_intl::LocaleId::into).collect())
            }),
            temperature_unit: src.temperature_unit.map(fidl_fuchsia_intl::TemperatureUnit::into),
            time_zone_id: src.time_zone_id.map_or(None, |tz| Some(tz.id)),
            hour_cycle: src.hour_cycle.map(fidl_fuchsia_settings::HourCycle::into),
        }
    }
}

/// Conversion from IntlInfo to IntlSettings
impl Into<fidl_fuchsia_settings::IntlSettings> for IntlInfo {
    fn into(self) -> IntlSettings {
        let mut intl_settings = IntlSettings::empty();

        intl_settings.locales = self
            .locales
            .map_or(None, |locales| Some(locales.into_iter().map(LocaleId::into).collect()));
        intl_settings.temperature_unit = self.temperature_unit.map(TemperatureUnit::into);
        intl_settings.time_zone_id =
            self.time_zone_id.map_or(None, |tz| Some(fidl_fuchsia_intl::TimeZoneId { id: tz }));
        intl_settings.hour_cycle = self.hour_cycle.map(HourCycle::into);

        intl_settings
    }
}

/// Unicode BCP-47 Locale Identifier
/// Reference LocaleId definition in
/// https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/intl.fidl
#[derive(PartialEq, Eq, Debug, Clone, Serialize, Deserialize)]
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

/// Selection of [temperature units](https://en.wikipedia.org/wiki/Degree_(temperature)).
/// Reference TemperatureUnit definition in
/// https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.intl/intl.fidl
#[derive(PartialEq, Eq, Debug, Clone, Copy, Serialize, Deserialize)]
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

/// Whether if the time format should be using 12 hour or 24 hour clock. H indicates the
/// maximum number that the hour indicator will ever show.
///
/// Reference HourCycle definition in
/// https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.settings/intl.fidl
#[derive(PartialEq, Eq, Debug, Clone, Copy, Serialize, Deserialize)]
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
