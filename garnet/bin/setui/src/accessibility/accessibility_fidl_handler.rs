// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use parking_lot::RwLock;
use std::sync::Arc;

use crate::fidl_processor::process_stream;
use futures::future::LocalBoxFuture;
use futures::FutureExt;

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_settings::{
    AccessibilityMarker, AccessibilityRequest, AccessibilityRequestStream,
    AccessibilitySetResponder, AccessibilitySettings, AccessibilityWatchResponder, Error,
};
use fuchsia_async as fasync;

use crate::switchboard::accessibility_types::{
    AccessibilityInfo, CaptionsSettings, ColorBlindnessType,
};
use crate::switchboard::base::{
    FidlResponseErrorLogger, SettingRequest, SettingResponse, SettingResponseResult, SettingType,
    Switchboard,
};
use crate::switchboard::hanging_get_handler::Sender;

impl Sender<AccessibilitySettings> for AccessibilityWatchResponder {
    fn send_response(self, data: AccessibilitySettings) {
        self.send(&mut Ok(data)).log_fidl_response_error(AccessibilityMarker::DEBUG_NAME);
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
            accessibility_settings.captions_settings =
                info.captions_settings.map(CaptionsSettings::into);

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
            captions_settings: settings
                .captions_settings
                .map(fidl_fuchsia_settings::CaptionsSettings::into),
        })
    }
}

pub fn spawn_accessibility_fidl_handler(
    switchboard_handle: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    stream: AccessibilityRequestStream,
) {
    process_stream::<AccessibilityMarker, AccessibilitySettings, AccessibilityWatchResponder>(
        stream,
        switchboard_handle,
        SettingType::Accessibility,
        Box::new(
            move |context, req| -> LocalBoxFuture<'_, Result<Option<AccessibilityRequest>, failure::Error>> {
                async move {
                    // Support future expansion of FIDL.
                    #[allow(unreachable_patterns)]
                    match req {
                        AccessibilityRequest::Set { settings, responder } => {
                            set_accessibility(context.switchboard.clone(), settings, responder);
                        }
                        AccessibilityRequest::Watch { responder } => {
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

/// Sends a request to set the accessibility settings through the switchboard and responds with an
/// appropriate result to the given responder.
fn set_accessibility(
    switchboard_handle: Arc<RwLock<dyn Switchboard + Send + Sync>>,
    settings: AccessibilitySettings,
    responder: AccessibilitySetResponder,
) {
    let switchboard_handle = switchboard_handle.clone();

    let (response_tx, response_rx) = futures::channel::oneshot::channel::<SettingResponseResult>();
    if switchboard_handle
        .write()
        .request(SettingType::Accessibility, settings.into(), response_tx)
        .is_ok()
    {
        fasync::spawn(async move {
            match response_rx.await {
                Ok(_) => responder
                    .send(&mut Ok(()))
                    .log_fidl_response_error(AccessibilityMarker::DEBUG_NAME),
                Err(_) => responder
                    .send(&mut Err(Error::Failed))
                    .log_fidl_response_error(AccessibilityMarker::DEBUG_NAME),
            }
        });
    } else {
        // report back an error immediately if we could not successfully
        // make the time zone set request. The return result can be ignored
        // as there is no actionable steps that can be taken.
        responder
            .send(&mut Err(Error::Failed))
            .log_fidl_response_error(AccessibilityMarker::DEBUG_NAME);
    }
}

#[cfg(test)]
mod tests {
    use crate::switchboard::accessibility_types::{
        CaptionFontFamily, CaptionFontStyle, ColorRgba, EdgeStyle,
    };
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
            captions_settings: None,
        };

        assert_eq!(request, SettingRequest::SetAccessibilityInfo(EXPECTED_ACCESSIBILITY_INFO));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_try_from_settings_request() {
        const TEST_COLOR: ColorRgba =
            ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 };
        const EXPECTED_FONT_STYLE: CaptionFontStyle = CaptionFontStyle {
            family: Some(CaptionFontFamily::Casual),
            color: Some(TEST_COLOR),
            relative_size: Some(1.0),
            char_edge_style: Some(EdgeStyle::Raised),
        };
        const EXPECTED_CAPTION_SETTINGS: CaptionsSettings = CaptionsSettings {
            for_media: Some(true),
            for_tts: Some(true),
            font_style: Some(EXPECTED_FONT_STYLE),
            window_color: Some(TEST_COLOR),
            background_color: Some(TEST_COLOR),
        };
        const EXPECTED_ACCESSIBILITY_INFO: AccessibilityInfo = AccessibilityInfo {
            audio_description: Some(true),
            screen_reader: Some(true),
            color_inversion: Some(true),
            enable_magnification: Some(true),
            color_correction: Some(
                crate::switchboard::accessibility_types::ColorBlindnessType::Protanomaly,
            ),
            captions_settings: Some(EXPECTED_CAPTION_SETTINGS),
        };

        let mut accessibility_settings = AccessibilitySettings::empty();
        accessibility_settings.audio_description = Some(true);
        accessibility_settings.screen_reader = Some(true);
        accessibility_settings.color_inversion = Some(true);
        accessibility_settings.enable_magnification = Some(true);
        accessibility_settings.color_correction = Some(ColorBlindnessType::Protanomaly);
        accessibility_settings.captions_settings = Some(EXPECTED_CAPTION_SETTINGS.into());

        let request = SettingRequest::from(accessibility_settings);

        assert_eq!(request, SettingRequest::SetAccessibilityInfo(EXPECTED_ACCESSIBILITY_INFO));
    }
}
