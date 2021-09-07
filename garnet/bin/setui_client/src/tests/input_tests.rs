// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{
    DeviceState, DeviceStateSource, DeviceType, InputDevice, InputDeviceSettings, InputMarker,
    InputRequest, InputSettings, Microphone, SourceState, ToggleStateFlags,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::{input, utils};

pub(crate) async fn validate_input(expected_mic_muted: Option<bool>) -> Result<(), Error> {
    let env = create_service!(Services::Input,
        InputRequest::Set { settings, responder } => {
            if let Some(Microphone { muted, .. }) = settings.microphone {
                assert_eq!(expected_mic_muted, muted);
                responder.send(&mut (Ok(())))?;
            }
        },
        InputRequest::Watch { responder } => {
            responder.send(InputDeviceSettings {
                microphone: Some(Microphone {
                    muted: expected_mic_muted,
                    ..Microphone::EMPTY
                }),
                ..InputDeviceSettings::EMPTY
            })?;
        }
    );

    let input_service =
        env.connect_to_protocol::<InputMarker>().context("Failed to connect to input service")?;

    let either = input::command(input_service, expected_mic_muted).await?;
    if expected_mic_muted.is_none() {
        if let utils::Either::Watch(mut stream) = either {
            let output = stream.try_next().await?.expect("Watch should have a result");
            assert_eq!(
                output,
                format!(
                    "{:#?}",
                    InputDeviceSettings {
                        microphone: Some(Microphone {
                            muted: expected_mic_muted,
                            ..Microphone::EMPTY
                        }),
                        ..InputDeviceSettings::EMPTY
                    }
                )
            );
        } else {
            panic!("Did not expect set result for a watch command");
        }
    } else if let utils::Either::Set(output) = either {
        assert_eq!(
            output,
            format!("Successfully set mic mute to {}\n", expected_mic_muted.unwrap())
        );
    } else {
        panic!("Did not expect watch result for a set command");
    }

    Ok(())
}

/// Creates a one-item list of input devices with the given properties.
fn create_input_devices(
    device_type: DeviceType,
    device_name: &str,
    device_state: u64,
) -> Vec<InputDevice> {
    let mut devices = Vec::new();
    let mut source_states = Vec::new();
    source_states.push(SourceState {
        source: Some(DeviceStateSource::Hardware),
        state: Some(DeviceState {
            toggle_flags: ToggleStateFlags::from_bits(1),
            ..DeviceState::EMPTY
        }),
        ..SourceState::EMPTY
    });
    source_states.push(SourceState {
        source: Some(DeviceStateSource::Software),
        state: Some(u64_to_state(device_state)),
        ..SourceState::EMPTY
    });
    let device = InputDevice {
        device_name: Some(device_name.to_string()),
        device_type: Some(device_type),
        source_states: Some(source_states),
        mutable_toggle_state: ToggleStateFlags::from_bits(12),
        state: Some(u64_to_state(device_state)),
        ..InputDevice::EMPTY
    };
    devices.push(device);
    devices
}

/// Transforms an u64 into an fuchsia_fidl_settings::DeviceState.
fn u64_to_state(num: u64) -> DeviceState {
    DeviceState { toggle_flags: ToggleStateFlags::from_bits(num), ..DeviceState::EMPTY }
}

pub(crate) async fn validate_input2_watch() -> Result<(), Error> {
    let env = create_service!(Services::Input,
        InputRequest::Watch2 { responder } => {
            responder.send(InputSettings {
                devices: Some(
                    create_input_devices(
                        DeviceType::Camera,
                        "camera",
                        1,
                    )
                ),
                ..InputSettings::EMPTY
            })?;
        }
    );

    let input_service =
        env.connect_to_protocol::<InputMarker>().context("Failed to connect to input service")?;

    let output = assert_watch!(input::command2(input_service, None, None, None));
    // Just check that the output contains some key strings that confirms the watch returned.
    // The string representation may not necessarily be in the same order.
    assert!(output.contains("Software"));
    assert!(output.contains("source_states: Some"));
    assert!(output.contains("toggle_flags: Some"));
    assert!(output.contains("camera"));
    assert!(output.contains("Available"));
    Ok(())
}

pub(crate) async fn validate_input2_set(
    device_type: DeviceType,
    device_name: &'static str,
    device_state: u64,
    expected_state_string: &str,
) -> Result<(), Error> {
    let env = create_service!(Services::Input,
        InputRequest::SetStates { input_states, responder } => {
            input_states.iter().for_each(move |state| {
                assert_eq!(Some(device_type), state.device_type);
                assert_eq!(Some(device_name.to_string()), state.name);
                assert_eq!(Some(u64_to_state(device_state)), state.state);
            });
            responder.send(&mut (Ok(())))?;
        }
    );

    let input_service =
        env.connect_to_protocol::<InputMarker>().context("Failed to connect to input service")?;

    let output = assert_set!(input::command2(
        input_service,
        Some(device_type),
        Some(device_name.to_string()),
        Some(u64_to_state(device_state)),
    ));
    // Just check that the output contains some key strings that confirms the set returned.
    // The string representation may not necessarily be in the same order.
    assert!(output.contains(&format!("{:?}", device_type)));
    assert!(output.contains(&format!("{:?}", device_name)));
    assert!(output.contains(expected_state_string));
    Ok(())
}
