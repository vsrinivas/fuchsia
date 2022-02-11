// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::{DisplaySettings, LowLightMode, Theme};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "display")]
/// get or set display settings
pub struct Display {
    /// the brightness value specified as a float in the range [0, 1]
    #[argh(option, short = 'b')]
    pub brightness: Option<f32>,

    /// the brightness values used to control auto brightness as a float in the range [0, 1]
    #[argh(option, short = 'o')]
    pub auto_brightness_level: Option<f32>,

    /// when set to 'true', enables auto brightness
    #[argh(option, short = 'a')]
    pub auto_brightness: Option<bool>,

    /// which low light mode setting to enable. Valid options are enable, disable, and
    /// disable_immediately
    #[argh(option, short = 'm', from_str_fn(str_to_low_light_mode))]
    pub low_light_mode: Option<LowLightMode>,

    /// which theme to set for the device. Valid options are default, dark, light, darkauto, and
    /// lightauto
    #[argh(option, short = 't', from_str_fn(str_to_theme))]
    pub theme: Option<Theme>,

    /// when set to 'true' the screen is enabled
    #[argh(option, short = 's')]
    pub screen_enabled: Option<bool>,
}

fn str_to_low_light_mode(src: &str) -> Result<fidl_fuchsia_settings::LowLightMode, String> {
    match src {
        "enable" | "e" => Ok(fidl_fuchsia_settings::LowLightMode::Enable),
        "disable" | "d" => Ok(fidl_fuchsia_settings::LowLightMode::Disable),
        "disable_immediately" | "i" => Ok(fidl_fuchsia_settings::LowLightMode::DisableImmediately),
        _ => Err(String::from("Couldn't parse low light mode")),
    }
}

fn str_to_theme(src: &str) -> Result<fidl_fuchsia_settings::Theme, String> {
    match src {
        "default" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Default),
            ..Theme::EMPTY
        }),
        "dark" => {
            Ok(Theme { theme_type: Some(fidl_fuchsia_settings::ThemeType::Dark), ..Theme::EMPTY })
        }
        "light" => {
            Ok(Theme { theme_type: Some(fidl_fuchsia_settings::ThemeType::Light), ..Theme::EMPTY })
        }
        "darkauto" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Dark),
            theme_mode: Some(fidl_fuchsia_settings::ThemeMode::AUTO),
            ..Theme::EMPTY
        }),
        "lightauto" => Ok(Theme {
            theme_type: Some(fidl_fuchsia_settings::ThemeType::Light),
            theme_mode: Some(fidl_fuchsia_settings::ThemeMode::AUTO),
            ..Theme::EMPTY
        }),
        _ => Err(String::from("Couldn't parse theme.")),
    }
}

impl From<Display> for DisplaySettings {
    fn from(src: Display) -> DisplaySettings {
        DisplaySettings {
            auto_brightness: src.auto_brightness,
            brightness_value: src.brightness,
            low_light_mode: src.low_light_mode,
            screen_enabled: src.screen_enabled,
            theme: src.theme,
            adjusted_auto_brightness: src.auto_brightness_level,
            ..DisplaySettings::EMPTY
        }
    }
}

impl From<DisplaySettings> for Display {
    fn from(src: DisplaySettings) -> Display {
        Display {
            auto_brightness: src.auto_brightness,
            brightness: src.brightness_value,
            low_light_mode: src.low_light_mode,
            screen_enabled: src.screen_enabled,
            theme: src.theme,
            auto_brightness_level: src.adjusted_auto_brightness,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["display"];

    #[test]
    fn test_display_cmd() {
        // Test input arguments are generated to according struct.
        let turned_on_auto = "true";
        let low_light_mode = "disable";
        let theme = "darkauto";
        let args = &["-a", turned_on_auto, "-m", low_light_mode, "-t", theme];
        assert_eq!(
            Display::from_args(CMD_NAME, args),
            Ok(Display {
                brightness: None,
                auto_brightness_level: None,
                auto_brightness: Some(true),
                low_light_mode: Some(str_to_low_light_mode(low_light_mode).unwrap()),
                theme: Some(str_to_theme(theme).unwrap()),
                screen_enabled: None,
            })
        )
    }
}
