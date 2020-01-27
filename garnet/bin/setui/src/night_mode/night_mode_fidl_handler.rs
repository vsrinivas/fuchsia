// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::fidl_processor::process_stream;

use futures::FutureExt;

use futures::future::LocalBoxFuture;

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    Error, NightModeMarker, NightModeRequest, NightModeRequestStream, NightModeSetResponder,
    NightModeSettings, NightModeWatchResponder,
};
use fuchsia_async as fasync;

use crate::switchboard::base::*;

use crate::switchboard::hanging_get_handler::Sender;

impl Sender<NightModeSettings> for NightModeWatchResponder {
    fn send_response(self, data: NightModeSettings) {
        self.send(data).log_fidl_response_error(NightModeMarker::DEBUG_NAME);
    }
}

impl From<SettingResponse> for NightModeSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::NightMode(info) = response {
            let mut night_mode_settings = NightModeSettings::empty();
            night_mode_settings.night_mode_enabled = info.night_mode_enabled;
            return night_mode_settings;
        }

        panic!("incorrect value sent to night_mode");
    }
}

impl From<NightModeSettings> for SettingRequest {
    fn from(settings: NightModeSettings) -> Self {
        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = settings.night_mode_enabled;
        SettingRequest::SetNightModeInfo(night_mode_info)
    }
}

pub fn spawn_night_mode_fidl_handler(
    switchboard: SwitchboardHandle,
    stream: NightModeRequestStream,
) {
    process_stream::<NightModeMarker, NightModeSettings, NightModeWatchResponder>(
        stream,
        switchboard,
        SettingType::NightMode,
        Box::new(
            move |context,
                  req|
                  -> LocalBoxFuture<'_, Result<Option<NightModeRequest>, anyhow::Error>> {
                async move {
                    #[allow(unreachable_patterns)]
                    match req {
                        NightModeRequest::Set { settings, responder } => {
                            set(context.switchboard, settings, responder).await;
                        }
                        NightModeRequest::Watch { responder } => {
                            context.watch(responder).await;
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

async fn set(
    switchboard_handle: SwitchboardHandle,
    settings: NightModeSettings,
    responder: NightModeSetResponder,
) {
    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    match switchboard_handle.lock().await.request(
        SettingType::NightMode,
        settings.into(),
        response_tx,
    ) {
        Ok(_) => {
            fasync::spawn(async move {
                let result = match response_rx.await {
                    Ok(Ok(_)) => responder.send(&mut Ok(())),
                    _ => responder.send(&mut Err(Error::Failed)),
                };
                result.log_fidl_response_error(NightModeMarker::DEBUG_NAME);
            });
        }
        Err(_) => {
            // Report back an error immediately if we could not successfully make the night mode
            // set request. The returned result can be ignored as there is no actionable steps
            // that can be taken.
            responder
                .send(&mut Err(Error::Failed))
                .log_fidl_response_error(NightModeMarker::DEBUG_NAME);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings_empty() {
        let request = SettingRequest::from(NightModeSettings::empty());
        let night_mode_info = NightModeInfo::empty();
        assert_eq!(request, SettingRequest::SetNightModeInfo(night_mode_info));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_from_settings() {
        const NIGHT_MODE_ENABLED: bool = true;

        let mut night_mode_settings = NightModeSettings::empty();
        night_mode_settings.night_mode_enabled = Some(NIGHT_MODE_ENABLED);
        let request = SettingRequest::from(night_mode_settings);

        let mut night_mode_info = NightModeInfo::empty();
        night_mode_info.night_mode_enabled = Some(NIGHT_MODE_ENABLED);

        assert_eq!(request, SettingRequest::SetNightModeInfo(night_mode_info));
    }
}
