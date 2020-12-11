// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_settings::{
        DeviceState, DeviceType, InputDeviceSettings, InputProxy, InputState, Microphone,
    },
};

// TODO(fxbug.dev/65686): Remove when clients are ported to new interface.
pub async fn command(proxy: InputProxy, mic_muted: Option<bool>) -> Result<String, Error> {
    let mut output = String::new();
    let mut input_settings = InputDeviceSettings::EMPTY;
    let mut microphone = Microphone::EMPTY;

    if mic_muted.is_some() {
        microphone.muted = mic_muted;
        input_settings.microphone = Some(microphone);
    }

    if input_settings == InputDeviceSettings::EMPTY {
        let setting_value = proxy.watch().await?;
        output.push_str(&format!("{:#?}", setting_value));
    } else {
        if let Err(err) = proxy.set(input_settings).await? {
            output.push_str(&format!("{:?}", err));
        } else if let Some(muted) = mic_muted {
            output.push_str(&format!("Successfully set mic mute to {:?}\n", muted));
        }
    }
    Ok(output)
}

// Used to interact with the new input interface.
// TODO(fxbug.dev/66186): Support multiple devices.
pub async fn command2(
    proxy: InputProxy,
    device_type: Option<DeviceType>,
    device_name: Option<String>,
    device_state: Option<DeviceState>,
) -> Result<String, Error> {
    let mut input_states = Vec::new();
    let mut input_state = InputState::EMPTY;

    if let Some(device_type) = device_type {
        input_state.device_type = Some(device_type);
    }
    if let Some(device_name) = device_name.clone() {
        input_state.name = Some(device_name);
    }
    if let Some(device_state) = device_state.clone() {
        input_state.state = Some(device_state);
    }

    if input_state == InputState::EMPTY {
        let setting_value = proxy.watch2().await?;
        Ok(format!("{:#?}", setting_value))
    } else {
        if device_type.is_none() {
            return Err(format_err!("Device type required"));
        }
        if device_state.is_none() {
            return Err(format_err!("Device state required"));
        }
        if device_name.is_none() {
            // Default device names.
            input_state.name = match device_type.unwrap() {
                DeviceType::Camera => Some("camera".to_string()),
                DeviceType::Microphone => Some("microphone".to_string()),
            };
        }
        input_states.push(input_state);
        let mut states = input_states.iter().map(|state| state.clone());
        if let Err(err) = proxy.set_states(&mut states).await? {
            Ok(format!("{:?}", err))
        } else {
            Ok(format!("Successfully set input states to {:#?}\n", input_states))
        }
    }
}
