// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::{
        AccessibilityInfo, SettingRequest, SettingResponse, SettingResponseResult, SettingType,
        Switchboard,
    },
    crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender},
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::{
        AccessibilityRequest, AccessibilityRequestStream, AccessibilitySetResponder,
        AccessibilitySettings, AccessibilityWatchResponder, Error,
    },
    fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::convert::TryFrom,
    std::sync::{Arc, RwLock},
};
type AccessibilityHangingGetHandler =
    Arc<Mutex<HangingGetHandler<AccessibilitySettings, AccessibilityWatchResponder>>>;

impl Sender<AccessibilitySettings> for AccessibilityWatchResponder {
    fn send_response(self, data: AccessibilitySettings) {
        self.send(&mut Ok(data)).unwrap();
    }
}

impl From<SettingResponse> for AccessibilitySettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Accessibility(info) = response {
            let mut accessibility_settings = AccessibilitySettings::empty();

            match info {
                AccessibilityInfo::AudioDescription(value) => {
                    accessibility_settings.audio_description = Some(value);
                }
            }

            return accessibility_settings;
        }

        panic!("incorrect value sent to accessibility");
    }
}

impl TryFrom<AccessibilitySettings> for SettingRequest {
    type Error = &'static str;

    fn try_from(settings: AccessibilitySettings) -> Result<Self, Self::Error> {
        if let Some(audio_description) = settings.audio_description {
            return Ok(SettingRequest::SetAudioDescription(audio_description));
        }

        Err("Failed to convert AccessibilitySettings to SettingRequest")
    }
}

fn to_request(settings: AccessibilitySettings) -> Option<SettingRequest> {
    settings.audio_description.map(SettingRequest::SetAudioDescription)
}

pub fn spawn_accessibility_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: AccessibilityRequestStream,
) {
    let switchboard_lock = switchboard_handle.clone();

    let hanging_get_handler: AccessibilityHangingGetHandler =
        HangingGetHandler::create(switchboard_handle, SettingType::Accessibility);

    fasync::spawn(async move {
        while let Ok(Some(req)) = stream.try_next().await {
            // Support future expansion of FIDL.
            #[allow(unreachable_patterns)]
            match req {
                AccessibilityRequest::Set { settings, responder } => {
                    set_accessibility(switchboard_lock.clone(), settings, responder);
                }
                AccessibilityRequest::Watch { responder } => {
                    let mut hanging_get_lock = hanging_get_handler.lock().await;
                    hanging_get_lock.watch(responder).await;
                }
                _ => {}
            }
        }
    });
}

/// Sends a request to set the accessibility settings through the switchboard and responds with an
/// appropriate result to the given responder.
fn set_accessibility(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    settings: AccessibilitySettings,
    responder: AccessibilitySetResponder,
) {
    match SettingRequest::try_from(settings) {
        Ok(request) => {
            let switchboard_handle = switchboard_handle.clone();

            let (response_tx, response_rx) =
                futures::channel::oneshot::channel::<SettingResponseResult>();
            if switchboard_handle
                .write()
                .unwrap()
                .request(SettingType::Accessibility, request, response_tx)
                .is_ok()
            {
                fasync::spawn(async move {
                    match response_rx.await {
                        Ok(_) => responder.send(&mut Ok(())).unwrap(),
                        Err(_) => responder.send(&mut Err(Error::Failed)).unwrap(),
                    }
                });
            } else {
                // report back an error immediately if we could not successfully
                // make the time zone set request. The return result can be ignored
                // as there is no actionable steps that can be taken.
                responder.send(&mut Err(Error::Failed)).ok();
            }
        }
        Err(_) => {
            responder.send(&mut Err(Error::Unsupported)).ok();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_try_from_settings_empty() {
        let request = SettingRequest::try_from(AccessibilitySettings::empty());

        assert_eq!(request.is_err(), true);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_try_from_settings() {
        const AUDIO_DESCRIPTION: bool = true;

        let mut accessibility_settings = AccessibilitySettings::empty();
        accessibility_settings.audio_description = Some(AUDIO_DESCRIPTION);

        let request = SettingRequest::try_from(accessibility_settings);

        assert_eq!(request, Ok(SettingRequest::SetAudioDescription(AUDIO_DESCRIPTION)));
    }
}
