// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingType;
use crate::handler::base::Request;
use crate::ingress::{request, watch, Scoped};
use crate::input::types::{DeviceStateSource, InputDevice, InputDeviceType};
use crate::job::source::{Error as JobError, ErrorResponder};
use crate::job::Job;
use fidl::endpoints::{ControlHandle, Responder};
use fidl_fuchsia_settings::{
    InputRequest, InputSetResponder, InputSetResult, InputSettings, InputState as FidlInputState,
    InputWatchResponder,
};
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use std::convert::TryFrom;

impl ErrorResponder for InputSetResponder {
    fn id(&self) -> &'static str {
        "Input_Set"
    }

    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error> {
        self.send(&mut Err(error))
    }
}

impl request::Responder<Scoped<InputSetResult>> for InputSetResponder {
    fn respond(self, Scoped(mut response): Scoped<InputSetResult>) {
        let _ = self.send(&mut response);
    }
}

impl watch::Responder<InputSettings, fuchsia_zircon::Status> for InputWatchResponder {
    fn respond(self, response: Result<InputSettings, fuchsia_zircon::Status>) {
        match response {
            Ok(settings) => {
                let _ = self.send(settings);
            }
            Err(error) => {
                self.control_handle().shutdown_with_epitaph(error);
            }
        }
    }
}

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

impl TryFrom<InputRequest> for Job {
    type Error = JobError;
    fn try_from(req: InputRequest) -> Result<Self, Self::Error> {
        // Support future expansion of FIDL.
        #[allow(unreachable_patterns)]
        match req {
            InputRequest::Set { input_states, responder } => match to_request(input_states) {
                Some(request) => {
                    Ok(request::Work::new(SettingType::Input, request, responder).into())
                }
                None => Err(JobError::InvalidInput(Box::new(responder))),
            },
            InputRequest::Watch { responder } => {
                Ok(watch::Work::new_job(SettingType::Input, responder))
            }
            _ => {
                fx_log_warn!("Received a call to an unsupported API: {:?}", req);
                Err(JobError::Unsupported)
            }
        }
    }
}
