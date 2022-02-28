// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use fidl_fuchsia_settings::InputState;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "input")]
/// get or set input settings
pub struct Input {
    #[argh(option, short = 't', from_str_fn(str_to_device_type))]
    /// the type of input device. Valid options are camera and microphone
    pub device_type: Option<fidl_fuchsia_settings::DeviceType>,

    #[argh(option, short = 'n', long = "name")]
    /// the name of the device. Must be unique within a device type
    pub device_name: Option<String>,

    #[argh(option, short = 's', long = "state", from_str_fn(str_to_device_state))]
    /// the device state flags, pass a comma separated string of the values available, active,
    /// muted, disabled and error. E.g. "-s available,active"
    pub device_state: Option<fidl_fuchsia_settings::DeviceState>,
}

fn str_to_device_type(src: &str) -> Result<fidl_fuchsia_settings::DeviceType, String> {
    let device_type = src.to_lowercase();
    match device_type.as_ref() {
        "microphone" | "m" => Ok(fidl_fuchsia_settings::DeviceType::Microphone),
        "camera" | "c" => Ok(fidl_fuchsia_settings::DeviceType::Camera),
        _ => Err(String::from("Unidentified device type")),
    }
}

fn str_to_device_state(src: &str) -> Result<fidl_fuchsia_settings::DeviceState, String> {
    use fidl_fuchsia_settings::ToggleStateFlags;

    Ok(fidl_fuchsia_settings::DeviceState {
        toggle_flags: Some(src.to_lowercase().split(",").fold(
            Ok(fidl_fuchsia_settings::ToggleStateFlags::empty()),
            |acc, flag| {
                acc.and_then(|acc| {
                    Ok(match flag {
                        "available" | "v" => ToggleStateFlags::AVAILABLE,
                        "active" | "a" => ToggleStateFlags::ACTIVE,
                        "muted" | "m" => ToggleStateFlags::MUTED,
                        "disabled" | "d" => ToggleStateFlags::DISABLED,
                        "error" | "e" => ToggleStateFlags::ERROR,
                        flag => {
                            return Err(format!("Unrecognized ToggleStateFlags value {:?}", flag))
                        }
                    } | acc)
                })
            },
        )?),
        ..fidl_fuchsia_settings::DeviceState::EMPTY
    })
}

impl From<Input> for InputState {
    fn from(src: Input) -> InputState {
        InputState {
            device_type: src.device_type,
            name: src.device_name,
            state: src.device_state,
            ..InputState::EMPTY
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["input"];

    #[test]
    fn test_display_cmd() {
        // Test input arguments are generated to according struct.
        let device_type = "microphone";
        let state = "muted";
        let args = &["-t", device_type, "-s", state];
        assert_eq!(
            Input::from_args(CMD_NAME, args),
            Ok(Input {
                device_type: Some(str_to_device_type(device_type).unwrap()),
                device_name: None,
                device_state: Some(str_to_device_state(state).unwrap()),
            })
        )
    }
}
