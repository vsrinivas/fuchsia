// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::KeyboardSettings;
use regex::Regex;
use std::num::ParseIntError;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone, Copy)]
#[argh(subcommand, name = "keyboard")]
/// get or set keyboard settings
pub struct Keyboard {
    /// keymap selection for the keyboard. Valid options are
    ///
    ///   - UsQwerty
    ///   - FrAzerty
    ///   - UsDvorak
    ///   - UsColemak
    ///
    #[argh(option, short = 'k', from_str_fn(str_to_keymap))]
    pub keymap: Option<fidl_fuchsia_input::KeymapId>,

    /// delay value of autorepeat values for the keyboard. Values should be a positive integer plus
    /// an SI time unit. Valid units are s, ms. If this value and autorepeat_period are zero, the
    /// autorepeat field of KeyboardSettings will be cleaned as None.
    #[argh(option, short = 'd', from_str_fn(str_to_duration))]
    pub autorepeat_delay: Option<i64>,

    /// period value of autorepeat values for the keyboard. Values should be a positive integer plus
    /// an SI time unit. Valid units are s, ms. If this value and autorepeat_delay are zero, the
    /// autorepeat field of KeyboardSettings will be cleaned as None.
    #[argh(option, short = 'p', from_str_fn(str_to_duration))]
    pub autorepeat_period: Option<i64>,
}

impl From<KeyboardSettings> for Keyboard {
    fn from(src: KeyboardSettings) -> Self {
        Keyboard {
            keymap: src.keymap,
            autorepeat_delay: src.autorepeat.map(|a| a.delay),
            autorepeat_period: src.autorepeat.map(|a| a.period),
        }
    }
}

impl From<Keyboard> for KeyboardSettings {
    fn from(src: Keyboard) -> KeyboardSettings {
        KeyboardSettings {
            keymap: src.keymap,
            autorepeat: if src.autorepeat_delay.is_none() && src.autorepeat_period.is_none() {
                None
            } else {
                Some(fidl_fuchsia_settings::Autorepeat {
                    delay: src.autorepeat_delay.unwrap_or(0),
                    period: src.autorepeat_period.unwrap_or(0),
                })
            },
            ..KeyboardSettings::EMPTY
        }
    }
}

/// Converts a single string of keymap id value into a fidl_fuchsia_input::KeymapId.
fn str_to_keymap(src: &str) -> Result<fidl_fuchsia_input::KeymapId, String> {
    Ok(match src.to_lowercase().as_str() {
        "usqwerty" => fidl_fuchsia_input::KeymapId::UsQwerty,
        "frazerty" => fidl_fuchsia_input::KeymapId::FrAzerty,
        "usdvorak" => fidl_fuchsia_input::KeymapId::UsDvorak,
        "uscolemak" => fidl_fuchsia_input::KeymapId::UsColemak,
        _ => return Err(String::from("Couldn't parse keymap id.")),
    })
}

/// Converts a single string of number and unit into a number interpreting as nanoseconds.
fn str_to_duration(src: &str) -> Result<i64, String> {
    // This regex matches a string that starts with at least one digit, follows by zero or more
    // whitespace, and ends with zero or more letters. Digits and letters are captured.
    let re = Regex::new(r"^(\d+)\s*([A-Za-z]*)$").unwrap();
    let captures = re.captures(src).ok_or_else(|| {
        String::from("Invalid input, please pass in number and units as <123ms> or <0>.")
    })?;

    let num: i64 = captures[1].parse().map_err(|e: ParseIntError| e.to_string())?;
    let unit = captures[2].to_string();

    Ok(match unit.to_lowercase().as_str() {
        "s" | "second" | "seconds" => num * 1_000_000_000,
        "ms" | "millisecond" | "milliseconds" => num * 1_000_000,
        _ => {
            if unit.is_empty() && num == 0 {
                0
            } else {
                return Err(String::from("Couldn't parse duration, please specify a valid unit."));
            }
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["keyboard"];

    #[test]
    fn test_keyboard_cmd() {
        // Test input arguments are generated to according struct.
        let keymap = "usqwerty";
        let num = "123s";
        let args = &["-k", keymap, "-d", num];
        assert_eq!(
            Keyboard::from_args(CMD_NAME, args),
            Ok(Keyboard {
                keymap: Some(str_to_keymap(keymap).unwrap()),
                autorepeat_delay: Some(str_to_duration(num).unwrap()),
                autorepeat_period: None
            })
        )
    }
}
