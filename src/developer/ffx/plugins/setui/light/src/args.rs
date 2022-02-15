// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::{LightState, LightValue};
use fidl_fuchsia_ui_types::ColorRgb;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "light")]
/// get or set light settings
pub struct LightGroup {
    #[argh(option, short = 'n')]
    /// name of a light group to set values for. Required if setting the value of a light group
    pub name: Option<String>,

    #[argh(option, short = 's')]
    /// repeated parameter for a list of simple on/off values to set for a light group.
    pub simple: Vec<bool>,

    #[argh(option, short = 'b')]
    /// repeated parameter for a list of floating point brightness values in the range [0, 1] for a
    /// light group
    pub brightness: Vec<f64>,

    #[argh(option, short = 'r', from_str_fn(str_to_rgb))]
    /// repeated parameter for a list of RGB values to set for a light group. Values should be in
    /// the range [0, 1] and specified as a comma-separated list of the red, green, and blue
    /// components. Ex. 0.1,0.4,0.23
    pub rgb: Vec<ColorRgb>,
}

/// Converts a comma-separated string of RGB values into a fidl_fuchsia_ui_types::ColorRgb.
fn str_to_rgb(src: &str) -> Result<fidl_fuchsia_ui_types::ColorRgb, String> {
    let mut part_iter =
        src.split(',').map(|p| p.parse::<f32>().map_err(|_| "failed to parse color value"));

    let color = {
        let local_ref = &mut part_iter;
        color_from_parts(local_ref)
    };
    match (color, part_iter.next()) {
        (Some(Ok(color)), None) => Ok(color),
        (Some(Err(err)), _) => Err(err),
        _ => Err(String::from("wrong number of values")),
    }
}

fn color_from_parts<'a, T>(
    part_iter: &mut T,
) -> Option<Result<fidl_fuchsia_ui_types::ColorRgb, String>>
where
    T: Iterator<Item = Result<f32, &'a str>>,
{
    Some(Ok(fidl_fuchsia_ui_types::ColorRgb {
        red: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
        green: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
        blue: match part_iter.next()? {
            Ok(c) => c,
            Err(e) => return Some(Err(e.to_string())),
        },
    }))
}

impl Into<Vec<LightState>> for LightGroup {
    fn into(self) -> Vec<LightState> {
        if self.simple.len() > 0 {
            return self
                .simple
                .clone()
                .into_iter()
                .map(|val| LightState { value: Some(LightValue::On(val)), ..LightState::EMPTY })
                .collect::<Vec<_>>();
        }

        if self.brightness.len() > 0 {
            return self
                .brightness
                .clone()
                .into_iter()
                .map(|val| LightState {
                    value: Some(LightValue::Brightness(val)),
                    ..LightState::EMPTY
                })
                .collect::<Vec<_>>();
        }

        if self.rgb.len() > 0 {
            return self
                .rgb
                .clone()
                .into_iter()
                .map(|val| LightState { value: Some(LightValue::Color(val)), ..LightState::EMPTY })
                .collect::<Vec<_>>();
        }

        return Vec::new();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["light"];

    #[test]
    fn test_light_cmd() {
        // Test input arguments are generated to according struct.
        let name = "test";
        let simple = "false";
        let brightness = "0.5";
        let rgb = "0.1,0.4,0.23";
        let args = &["-n", name, "-s", simple, "-b", brightness, "-r", rgb];
        assert_eq!(
            LightGroup::from_args(CMD_NAME, args),
            Ok(LightGroup {
                name: Some(name.to_string()),
                simple: vec![false],
                brightness: vec![0.5],
                rgb: vec![str_to_rgb(rgb).unwrap()],
            })
        )
    }
}
