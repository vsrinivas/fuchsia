// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::fidl_hanging_get_responder,
    crate::fidl_processor::process_stream,
    crate::switchboard::base::{SettingRequest, SettingResponse, SettingType, SwitchboardClient},
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{
        Error, InputDeviceSettings, InputMarker, InputRequest, InputRequestStream,
        InputSetResponder, InputWatchResponder, Microphone,
    },
    fuchsia_async as fasync,
    futures::future::LocalBoxFuture,
    futures::prelude::*,
};

fidl_hanging_get_responder!(InputDeviceSettings, InputWatchResponder, InputMarker::DEBUG_NAME);

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

pub fn spawn_input_fidl_handler(switchboard_client: SwitchboardClient, stream: InputRequestStream) {
    process_stream::<InputMarker, InputDeviceSettings, InputWatchResponder>(
        stream,
        switchboard_client,
        SettingType::Input,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<InputRequest>, anyhow::Error>> {
                async move {
                    // Support future expansion of FIDL.
                    #[allow(unreachable_patterns)]
                    match req {
                        InputRequest::Set { settings, responder } => {
                            if let Some(request) = to_request(settings) {
                                set_mic_mute(&context.switchboard_client, request, responder).await
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
                .boxed_local()
            },
        ),
    );
}

/// Send the set request to the switchboard and send a response back over the [responder].
async fn set_mic_mute(
    switchboard_client: &SwitchboardClient,
    request: SettingRequest,
    responder: InputSetResponder,
) {
    if let Ok(response_rx) = switchboard_client.request(SettingType::Input, request).await {
        fasync::spawn(async move {
            // Return success if we get a Ok result from the switchboard.
            if let Ok(Ok(_)) = response_rx.await {
                responder.send(&mut Ok(())).log_fidl_response_error(InputMarker::DEBUG_NAME);
            } else {
                responder
                    .send(&mut Err(fidl_fuchsia_settings::Error::Failed))
                    .log_fidl_response_error(InputMarker::DEBUG_NAME);
            }
        });
    } else {
        responder
            .send(&mut Err(fidl_fuchsia_settings::Error::Failed))
            .log_fidl_response_error(InputMarker::DEBUG_NAME);
    }
}
