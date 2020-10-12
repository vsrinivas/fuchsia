// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_settings::{
    AccessibilityMarker, AccessibilityRequest, AccessibilitySettings, AccessibilityWatch2Responder,
    AccessibilityWatchResponder,
};
use fuchsia_async as fasync;

use crate::fidl_hanging_get_responder;
use crate::fidl_process;
use crate::fidl_processor::settings::RequestContext;
use crate::request_respond;
use crate::switchboard::accessibility_types::{
    AccessibilityInfo, CaptionsSettings, ColorBlindnessType,
};
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingType};

fidl_hanging_get_responder!(
    AccessibilityMarker,
    AccessibilitySettings,
    AccessibilityWatchResponder,
    AccessibilitySettings,
    AccessibilityWatch2Responder,
);

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

fidl_process!(
    Accessibility,
    SettingType::Accessibility,
    process_request,
    SettingType::Accessibility,
    AccessibilitySettings,
    AccessibilityWatch2Responder,
    process_request_2,
);

async fn process_request(
    context: RequestContext<AccessibilitySettings, AccessibilityWatchResponder>,
    req: AccessibilityRequest,
) -> Result<Option<AccessibilityRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        AccessibilityRequest::Set { settings, responder } => {
            fasync::Task::spawn(async move {
                request_respond!(
                    context,
                    responder,
                    SettingType::Accessibility,
                    settings.into(),
                    Ok(()),
                    Err(fidl_fuchsia_settings::Error::Failed),
                    AccessibilityMarker
                );
            })
            .detach();
        }
        AccessibilityRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

// TODO(fxbug.dev/55719): Remove when clients are ported to watch.
async fn process_request_2(
    context: RequestContext<AccessibilitySettings, AccessibilityWatch2Responder>,
    req: AccessibilityRequest,
) -> Result<Option<AccessibilityRequest>, anyhow::Error> {
    // Support future expansion of FIDL.
    #[allow(unreachable_patterns)]
    match req {
        AccessibilityRequest::Watch2 { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}

#[cfg(test)]
mod tests {
    use crate::switchboard::accessibility_types::{
        CaptionFontFamily, CaptionFontStyle, ColorRgba, EdgeStyle,
    };
    use fidl_fuchsia_settings::ColorBlindnessType;

    use super::*;

    #[test]
    fn test_request_try_from_settings_request_empty() {
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

    #[test]
    fn test_try_from_settings_request() {
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
