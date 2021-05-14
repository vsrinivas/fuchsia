// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::base::{SettingInfo, SettingType},
    crate::fidl_common::FidlResponseErrorLogger,
    crate::fidl_hanging_get_responder,
    crate::fidl_process_custom,
    crate::fidl_processor::settings::RequestContext,
    crate::handler::base::Request,
    crate::input::input_controller::DEFAULT_MIC_NAME,
    crate::input::types::{DeviceState, DeviceStateSource, InputDevice, InputDeviceType},
    crate::request_respond,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_settings::{
        Error, InputDeviceSettings, InputMarker, InputRequest, InputSettings,
        InputState as FidlInputState, InputWatch2Responder, InputWatchResponder, Microphone,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    std::collections::HashMap,
};

fidl_hanging_get_responder!(InputMarker, InputSettings, InputWatch2Responder);
fidl_hanging_get_responder!(InputMarker, InputDeviceSettings, InputWatchResponder);

// TODO(fxbug.dev/65686): Remove when clients are ported over to new version.
impl From<SettingInfo> for InputDeviceSettings {
    fn from(response: SettingInfo) -> Self {
        if let SettingInfo::Input(info) = response {
            let mut input_settings = InputDeviceSettings::EMPTY;
            let mic_state = info
                .input_device_state
                .get_state(InputDeviceType::MICROPHONE, DEFAULT_MIC_NAME.to_string());
            let mic_muted =
                mic_state.unwrap_or_else(|_| DeviceState::new()).has_state(DeviceState::MUTED);

            let microphone = Microphone { muted: Some(mic_muted), ..Microphone::EMPTY };

            input_settings.microphone = Some(microphone);
            input_settings
        } else {
            panic!("Incorrect value sent to input");
        }
    }
}

fn to_request_2(fidl_input_states: Vec<FidlInputState>) -> Option<Request> {
    // Every device requires at least a device type and state flags.
    let input_states_invalid_args: Vec<&FidlInputState> = fidl_input_states
        .iter()
        .filter(|input_state| input_state.device_type.is_none() || input_state.state.is_none())
        .collect();

    // If any devices were filtered out, the args were invalid, so exit.
    if !input_states_invalid_args.is_empty() {
        fx_log_err!("Failed to parse input request: missing args");
        return None;
    }

    let input_states = fidl_input_states
        .iter()
        .map(|input_state| {
            let device_type: InputDeviceType = input_state.device_type.unwrap().into();
            let device_state = input_state.state.clone().unwrap().into();
            let device_name = input_state.name.clone().unwrap_or_else(|| device_type.to_string());
            let mut source_states = HashMap::<DeviceStateSource, DeviceState>::new();

            source_states.insert(DeviceStateSource::SOFTWARE, device_state);
            InputDevice { name: device_name, device_type, state: device_state, source_states }
        })
        .collect();

    Some(Request::SetInputStates(input_states))
}

// TODO(fxbug.dev/65686): Remove when clients are ported over to new version.
fn to_request(settings: InputDeviceSettings) -> Option<Request> {
    if let Some(Microphone { muted: Some(muted), .. }) = settings.microphone {
        Some(Request::SetMicMute(muted))
    } else {
        None
    }
}

fidl_process_custom!(
    Input,
    SettingType::Input,
    InputWatchResponder,
    InputDeviceSettings,
    process_request,
    SettingType::Input,
    InputWatch2Responder,
    InputSettings,
    process_request_2,
);

async fn process_request_2(
    context: RequestContext<InputSettings, InputWatch2Responder>,
    req: InputRequest,
) -> Result<Option<InputRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        InputRequest::SetStates { input_states, responder } => {
            if let Some(request) = to_request_2(input_states) {
                fasync::Task::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::Input,
                        request,
                        Ok(()),
                        Err(fidl_fuchsia_settings::Error::Failed),
                        InputMarker
                    );
                })
                .detach();
            } else {
                responder
                    .send(&mut Err(Error::Unsupported))
                    .log_fidl_response_error(InputMarker::DEBUG_NAME);
            }
        }
        InputRequest::Watch2 { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}

// TODO(fxbug.dev/65686): Remove when clients are ported over to new version
async fn process_request(
    context: RequestContext<InputDeviceSettings, InputWatchResponder>,
    req: InputRequest,
) -> Result<Option<InputRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        InputRequest::Set { settings, responder } => {
            if let Some(request) = to_request(settings) {
                fasync::Task::spawn(async move {
                    request_respond!(
                        context,
                        responder,
                        SettingType::Input,
                        request,
                        Ok(()),
                        Err(fidl_fuchsia_settings::Error::Failed),
                        InputMarker
                    );
                })
                .detach();
            } else {
                responder
                    .send(&mut Err(Error::Unsupported))
                    .log_fidl_response_error(InputMarker::DEBUG_NAME);
            }
        }
        InputRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    Ok(None)
}
