// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_hanging_get_responder,
    crate::fidl_process,
    crate::fidl_processor::settings::RequestContext,
    crate::request_respond,
    crate::switchboard::base::{SettingRequest, SettingResponse, SettingType},
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        Error, InputDeviceSettings, InputMarker, InputRequest, InputWatchResponder, Microphone,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
};

fidl_hanging_get_responder!(InputMarker, InputDeviceSettings, InputWatchResponder);

impl From<SettingResponse> for InputDeviceSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Input(info) = response {
            let mut input_settings = InputDeviceSettings::empty();

            let microphone = Microphone { muted: Some(info.microphone.muted) };

            input_settings.microphone = Some(microphone);
            input_settings
        } else {
            panic!("Incorrect value sent to input");
        }
    }
}

fn to_request(settings: InputDeviceSettings) -> Option<SettingRequest> {
    if let Some(Microphone { muted: Some(muted) }) = settings.microphone {
        Some(SettingRequest::SetMicMute(muted))
    } else {
        None
    }
}

fidl_process!(Input, SettingType::Input, InputDeviceSettings, process_request);

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
                        InputMarker::DEBUG_NAME
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
    return Ok(None);
}
