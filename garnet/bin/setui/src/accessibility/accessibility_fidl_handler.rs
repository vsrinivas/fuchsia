// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{Arc, RwLock};

use futures::lock::Mutex;
use futures::TryStreamExt;

use fidl_fuchsia_settings::{
    AccessibilityRequest, AccessibilityRequestStream, AccessibilitySetResponder,
    AccessibilitySettings, AccessibilityWatchResponder, Error,
};
use fuchsia_async as fasync;

use crate::switchboard::base::{
    AccessibilityInfo, ColorBlindnessType, SettingRequest, SettingResponse, SettingResponseResult,
    SettingType, Switchboard,
};
use crate::switchboard::hanging_get_handler::{HangingGetHandler, Sender};
use crate::switchboard::switchboard_impl::SwitchboardImpl;

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

            accessibility_settings.audio_description = info.audio_description;
            accessibility_settings.screen_reader = info.screen_reader;
            accessibility_settings.color_inversion = info.color_inversion;
            accessibility_settings.enable_magnification = info.enable_magnification;
            accessibility_settings.color_correction =
                info.color_correction.map(ColorBlindnessType::into);

            return accessibility_settings;
        }

        panic!("incorrect value sent to accessibility");
    }
}

impl From<AccessibilitySettings> for SettingRequest {
    fn from(settings: AccessibilitySettings) -> Self {
        SettingRequest::SetAccessibilityInfo(AccessibilityInfo {
            audio_description: settings.audio_description,
            screen_reader: settings.screen_reader,
            color_inversion: settings.color_inversion,
            enable_magnification: settings.enable_magnification,
            color_correction: settings
                .color_correction
                .map(fidl_fuchsia_settings::ColorBlindnessType::into),
        })
    }
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
    let switchboard_handle = switchboard_handle.clone();

    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard_handle
        .write()
        .unwrap()
        .request(SettingType::Accessibility, settings.into(), response_tx)
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

#[cfg(test)]
mod tests {
    use fidl_fuchsia_settings::ColorBlindnessType;

    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_request_try_from_settings_request_empty() {
        let request = SettingRequest::from(AccessibilitySettings::empty());

        const EXPECTED_ACCESSIBILITY_INFO: AccessibilityInfo = AccessibilityInfo {
            audio_description: None,
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
        };

        assert_eq!(request, SettingRequest::SetAccessibilityInfo(EXPECTED_ACCESSIBILITY_INFO));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_try_from_settings_request() {
        const EXPECTED_ACCESSIBILITY_INFO: AccessibilityInfo = AccessibilityInfo {
            audio_description: Some(true),
            screen_reader: Some(true),
            color_inversion: Some(true),
            enable_magnification: Some(true),
            color_correction: Some(crate::switchboard::base::ColorBlindnessType::Protanomaly),
        };

        let mut accessibility_settings = AccessibilitySettings::empty();
        accessibility_settings.audio_description = Some(true);
        accessibility_settings.screen_reader = Some(true);
        accessibility_settings.color_inversion = Some(true);
        accessibility_settings.enable_magnification = Some(true);
        accessibility_settings.color_correction = Some(ColorBlindnessType::Protanomaly);
        accessibility_settings.captions_settings = None;

        let request = SettingRequest::from(accessibility_settings);

        assert_eq!(request, SettingRequest::SetAccessibilityInfo(EXPECTED_ACCESSIBILITY_INFO));
    }
}
