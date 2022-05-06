// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::fidl_common::FidlResponseErrorLogger;
use crate::fidl_hanging_get_responder;
use crate::fidl_process_custom;
use crate::fidl_processor::settings::RequestContext;
use crate::handler::base::Request;
use crate::input::types::{DeviceStateSource, InputDevice, InputDeviceType};
use crate::request_respond;
use fidl::endpoints::ProtocolMarker;
use fidl_fuchsia_settings::{
    Error, InputMarker, InputRequest, InputSettings, InputState as FidlInputState,
    InputWatch2Responder, InputWatchResponder,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;

fidl_hanging_get_responder!(InputMarker, InputSettings, InputWatch2Responder);
fidl_hanging_get_responder!(InputMarker, InputSettings, InputWatchResponder);

fn to_request(fidl_input_states: Vec<FidlInputState>) -> Option<Request> {
    // Every device requires at least a device type and state flags.
    let mut input_states_invalid_args = fidl_input_states
        .iter()
        .filter(|input_state| input_state.device_type.is_none() || input_state.state.is_none());

    // If any devices were filtered out, the args were invalid, so exit.
    if input_states_invalid_args.next().is_some() {
        fx_log_err!("Failed to parse input request: missing args");
        return None;
    }

    let input_states = fidl_input_states
        .iter()
        .map(|input_state| {
            let device_type: InputDeviceType = input_state.device_type.unwrap().into();
            let device_state = input_state.state.clone().unwrap().into();
            let device_name = input_state.name.clone().unwrap_or_else(|| device_type.to_string());
            let source_states = [(DeviceStateSource::SOFTWARE, device_state)].into();
            InputDevice { name: device_name, device_type, state: device_state, source_states }
        })
        .collect();

    Some(Request::SetInputStates(input_states))
}

fidl_process_custom!(
    Input,
    SettingType::Input,
    InputWatchResponder,
    InputSettings,
    process_request,
    SettingType::Input,
    InputWatch2Responder,
    InputSettings,
    process_request_2,
);

// TODO(fxbug.dev/65686): Remove when clients are ported over to new version
async fn process_request_2(
    context: RequestContext<InputSettings, InputWatch2Responder>,
    req: InputRequest,
) -> Result<Option<InputRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        InputRequest::SetStates { input_states, responder } => {
            if let Some(request) = to_request(input_states) {
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

async fn process_request(
    context: RequestContext<InputSettings, InputWatchResponder>,
    req: InputRequest,
) -> Result<Option<InputRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        InputRequest::Set { input_states, responder } => {
            if let Some(request) = to_request(input_states) {
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
