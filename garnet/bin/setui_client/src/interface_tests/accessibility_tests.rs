// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use crate::{accessibility, Accessibility, CaptionCommands, CaptionOptions};
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{
    AccessibilityMarker, AccessibilityRequest, AccessibilitySettings, CaptionFontFamily,
    ColorBlindnessType, EdgeStyle,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use uuid::Uuid;

#[fuchsia_async::run_until_stalled(test)]
async fn validate_accessibility_set() -> Result<(), Error> {
    const TEST_COLOR: fidl_fuchsia_ui_types::ColorRgba =
        fidl_fuchsia_ui_types::ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 };
    let expected_options: Accessibility = Accessibility {
        audio_description: Some(true),
        screen_reader: Some(true),
        color_inversion: Some(false),
        enable_magnification: Some(false),
        color_correction: Some(ColorBlindnessType::Protanomaly),
        caption_options: Some(CaptionCommands::CaptionOptions(CaptionOptions {
            for_media: Some(true),
            for_tts: Some(false),
            window_color: Some(TEST_COLOR),
            background_color: Some(TEST_COLOR),
            font_family: Some(CaptionFontFamily::Cursive),
            font_color: Some(TEST_COLOR),
            relative_size: Some(1.0),
            char_edge_style: Some(EdgeStyle::Raised),
        })),
    };

    let test_assertions = {
        let expected_options = expected_options.clone();
        move |settings: AccessibilitySettings| {
            assert_eq!(expected_options.audio_description, settings.audio_description);
            assert_eq!(expected_options.screen_reader, settings.screen_reader);
            assert_eq!(expected_options.color_inversion, settings.color_inversion);
            assert_eq!(expected_options.enable_magnification, settings.enable_magnification);
            assert_eq!(expected_options.color_correction, settings.color_correction);

            // If no caption options are provided, then captions_settings field in service should
            // also be None. The inverse of this should also be true.
            assert_eq!(
                expected_options.caption_options.is_some(),
                settings.captions_settings.is_some()
            );
            match (settings.captions_settings, expected_options.caption_options) {
                (Some(captions_settings), Some(caption_settings_enum)) => {
                    let CaptionCommands::CaptionOptions(input) = caption_settings_enum;

                    assert_eq!(input.for_media, captions_settings.for_media);
                    assert_eq!(input.for_tts, captions_settings.for_tts);
                    assert_eq!(input.window_color, captions_settings.window_color);
                    assert_eq!(input.background_color, captions_settings.background_color);

                    if let Some(font_style) = captions_settings.font_style {
                        assert_eq!(input.font_family, font_style.family);
                        assert_eq!(input.font_color, font_style.color);
                        assert_eq!(input.relative_size, font_style.relative_size);
                        assert_eq!(input.char_edge_style, font_style.char_edge_style);
                    }
                }
                _ => {}
            }
        }
    };

    let mut fs = ServiceFs::new();
    fs.add_fidl_service(Services::Accessibility);

    let mut uuid = Uuid::new_v4().to_string();
    uuid.push_str(ENV_NAME);
    let env = fs.create_nested_environment(&uuid)?;

    fasync::Task::spawn(fs.for_each_concurrent(None, {
        let test_assertions = test_assertions.clone();
        move |connection| {
            let test_assertions = test_assertions.clone();
            async move {
                if let Services::Accessibility(stream) = connection {
                    stream
                        .err_into::<anyhow::Error>()
                        .try_for_each(|req| {
                            let test_assertions = test_assertions.clone();
                            async move {
                                match req {
                                    AccessibilityRequest::Set { settings, responder } => {
                                        test_assertions(settings);
                                        responder.send(&mut Ok(()))?;
                                    }
                                    _ => panic!("Incorrect command to service"),
                                }
                                Ok(())
                            }
                        })
                        .unwrap_or_else(|e: anyhow::Error| {
                            panic!("error running setui server: {:?}", e)
                        })
                        .await;
                }
            }
        }
    }))
    .detach();

    let accessibility_service = env
        .connect_to_protocol::<AccessibilityMarker>()
        .context("Failed to connect to accessibility service")?;

    let output = assert_set!(accessibility::command(accessibility_service, expected_options));
    assert_eq!(output, "Successfully set AccessibilitySettings");
    Ok(())
}

#[fuchsia_async::run_until_stalled(test)]
async fn validate_accessibility_watch() -> Result<(), Error> {
    let env = create_service!(
        Services::Accessibility,
        AccessibilityRequest::Watch { responder } => {
            responder.send(AccessibilitySettings::EMPTY)?;
        }
    );

    let accessibility_service = env
        .connect_to_protocol::<AccessibilityMarker>()
        .context("Failed to connect to accessibility service")?;

    let output =
        assert_watch!(accessibility::command(accessibility_service, Accessibility::default()));
    assert_eq!(output, format!("{:#?}", AccessibilitySettings::EMPTY));
    Ok(())
}
