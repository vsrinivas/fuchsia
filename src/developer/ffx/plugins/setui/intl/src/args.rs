// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_intl::{LocaleId, TemperatureUnit, TimeZoneId};
use fidl_fuchsia_settings::{HourCycle, IntlSettings};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "intl")]
/// get or set internationalization settings
pub struct Intl {
    /// a valid timezone matching the data available at https://www.iana.org/time-zones
    #[argh(option, short = 'z', from_str_fn(str_to_time_zone))]
    pub time_zone: Option<TimeZoneId>,

    #[argh(option, short = 'u', from_str_fn(str_to_temperature_unit))]
    /// the unit to use for temperature. Valid options are celsius and fahrenheit
    pub temperature_unit: Option<TemperatureUnit>,

    #[argh(option, short = 'l', from_str_fn(str_to_locale))]
    /// list of locales, separated by spaces, formatted by Unicode BCP-47 Locale Identifier, e.g.
    /// en-us
    pub locales: Vec<LocaleId>,

    /// the hour cycle to use. Valid options are h11 for 12-hour clock with 0:10 am after midnight,
    /// h12 for 12-hour clock with 12:10am after midnight, h23 for 24-hour clock with 0:10 after
    /// midnight, and h24 for 24-hour clock with 24:10 after midnight
    #[argh(option, short = 'h', from_str_fn(str_to_hour_cycle))]
    pub hour_cycle: Option<HourCycle>,

    #[argh(switch)]
    /// if set, this flag will set locales as an empty list. Overrides the locales arguments
    pub clear_locales: bool,
}

fn str_to_time_zone(src: &str) -> Result<fidl_fuchsia_intl::TimeZoneId, String> {
    Ok(fidl_fuchsia_intl::TimeZoneId { id: src.to_string() })
}

fn str_to_temperature_unit(src: &str) -> Result<fidl_fuchsia_intl::TemperatureUnit, String> {
    match src.to_lowercase().as_str() {
        "c" | "celsius" => Ok(fidl_fuchsia_intl::TemperatureUnit::Celsius),
        "f" | "fahrenheit" => Ok(fidl_fuchsia_intl::TemperatureUnit::Fahrenheit),
        _ => Err(String::from("Couldn't parse temperature")),
    }
}

fn str_to_locale(src: &str) -> Result<fidl_fuchsia_intl::LocaleId, String> {
    Ok(fidl_fuchsia_intl::LocaleId { id: src.to_string() })
}

fn str_to_hour_cycle(src: &str) -> Result<fidl_fuchsia_settings::HourCycle, String> {
    match src.to_lowercase().as_str() {
        "unknown" => Ok(fidl_fuchsia_settings::HourCycle::Unknown),
        "h11" => Ok(fidl_fuchsia_settings::HourCycle::H11),
        "h12" => Ok(fidl_fuchsia_settings::HourCycle::H12),
        "h23" => Ok(fidl_fuchsia_settings::HourCycle::H23),
        "h24" => Ok(fidl_fuchsia_settings::HourCycle::H24),
        _ => Err(String::from("Couldn't parse hour cycle")),
    }
}

impl From<Intl> for IntlSettings {
    fn from(src: Intl) -> IntlSettings {
        IntlSettings {
            locales: if src.clear_locales {
                Some(vec![])
            } else if src.locales.len() > 0 {
                Some(src.locales)
            } else {
                // No locales specified and clear_locales not set, don't change the locales value.
                None
            },
            temperature_unit: src.temperature_unit,
            time_zone_id: src.time_zone,
            hour_cycle: src.hour_cycle,
            ..IntlSettings::EMPTY
        }
    }
}

impl From<IntlSettings> for Intl {
    fn from(src: IntlSettings) -> Intl {
        Intl {
            locales: src.locales.clone().unwrap_or(vec![]),
            temperature_unit: src.temperature_unit,
            time_zone: src.time_zone_id,
            hour_cycle: src.hour_cycle,
            clear_locales: if src.locales.is_some() && src.locales.unwrap() == vec![] {
                true
            } else {
                false
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["intl"];

    #[test]
    fn test_intl_cmd() {
        // Test input arguments are generated to according struct.
        let time_zone = "GTM";
        let unit = "celsius";
        let locales = "fr-u-hc-h12";
        let hour_cycle = "h12";
        let args = &["-z", time_zone, "-u", unit, "-l", locales, "-h", hour_cycle];
        assert_eq!(
            Intl::from_args(CMD_NAME, args),
            Ok(Intl {
                time_zone: Some(str_to_time_zone(time_zone).unwrap()),
                temperature_unit: Some(str_to_temperature_unit(unit).unwrap()),
                locales: vec![str_to_locale(locales).unwrap()],
                hour_cycle: Some(str_to_hour_cycle(hour_cycle).unwrap()),
                clear_locales: false,
            })
        )
    }
}
