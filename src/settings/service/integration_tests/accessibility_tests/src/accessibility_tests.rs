// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::AccessibilityTest;
use fidl_fuchsia_settings::{
    AccessibilitySettings, CaptionFontFamily, CaptionFontStyle, CaptionsSettings,
    ColorBlindnessType, EdgeStyle,
};
use fidl_fuchsia_ui_types::ColorRgba;
mod common;

#[fuchsia::test]
async fn test_accessibility_set_all() {
    const CHANGED_COLOR_BLINDNESS_TYPE: ColorBlindnessType = ColorBlindnessType::Tritanomaly;
    const TEST_COLOR: ColorRgba = ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 };
    const CHANGED_FONT_STYLE: CaptionFontStyle = CaptionFontStyle {
        family: Some(CaptionFontFamily::Casual),
        color: Some(TEST_COLOR),
        relative_size: Some(1.0),
        char_edge_style: Some(EdgeStyle::Raised),
        ..CaptionFontStyle::EMPTY
    };
    const CHANGED_CAPTION_SETTINGS: CaptionsSettings = CaptionsSettings {
        for_media: Some(true),
        for_tts: Some(true),
        font_style: Some(CHANGED_FONT_STYLE),
        window_color: Some(TEST_COLOR),
        background_color: Some(TEST_COLOR),
        ..CaptionsSettings::EMPTY
    };

    let initial_settings = AccessibilitySettings::EMPTY;

    let mut expected_settings = AccessibilitySettings::EMPTY;
    expected_settings.audio_description = Some(true);
    expected_settings.screen_reader = Some(true);
    expected_settings.color_inversion = Some(true);
    expected_settings.enable_magnification = Some(true);
    expected_settings.color_correction = Some(CHANGED_COLOR_BLINDNESS_TYPE.into());
    expected_settings.captions_settings = Some(CHANGED_CAPTION_SETTINGS);

    let instance = AccessibilityTest::create_realm().await.expect("setting up test realm");

    {
        let proxy = AccessibilityTest::connect_to_accessibilitymarker(&instance);

        // Make a watch call.
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings, initial_settings);

        // Ensure setting interface propagates correctly
        proxy.set(expected_settings.clone()).await.expect("set completed").expect("set successful");
    }

    {
        let proxy = AccessibilityTest::connect_to_accessibilitymarker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings, expected_settings);
    }

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_accessibility_set_captions() {
    const CHANGED_FONT_STYLE: CaptionFontStyle = CaptionFontStyle {
        family: Some(CaptionFontFamily::Casual),
        color: None,
        relative_size: Some(1.0),
        char_edge_style: None,
        ..CaptionFontStyle::EMPTY
    };
    const EXPECTED_CAPTIONS_SETTINGS: CaptionsSettings = CaptionsSettings {
        for_media: Some(true),
        for_tts: None,
        font_style: Some(CHANGED_FONT_STYLE),
        window_color: Some(ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 }),
        background_color: None,
        ..CaptionsSettings::EMPTY
    };

    let mut expected_settings = AccessibilitySettings::EMPTY;
    expected_settings.captions_settings = Some(EXPECTED_CAPTIONS_SETTINGS);

    let instance = AccessibilityTest::create_realm().await.expect("setting up test realm");

    {
        let proxy = AccessibilityTest::connect_to_accessibilitymarker(&instance);

        // Set for_media and window_color in the top-level CaptionsSettings.
        let mut first_set = AccessibilitySettings::EMPTY;
        first_set.captions_settings = Some(CaptionsSettings {
            for_media: Some(false),
            for_tts: None,
            font_style: None,
            window_color: EXPECTED_CAPTIONS_SETTINGS.window_color,
            background_color: None,
            ..CaptionsSettings::EMPTY
        });
        proxy.set(first_set.clone()).await.expect("set completed").expect("set successful");

        // Set FontStyle and overwrite for_media.
        let mut second_set = AccessibilitySettings::EMPTY;
        second_set.captions_settings = Some(CaptionsSettings {
            for_media: EXPECTED_CAPTIONS_SETTINGS.for_media,
            for_tts: None,
            font_style: EXPECTED_CAPTIONS_SETTINGS.font_style,
            window_color: None,
            background_color: None,
            ..CaptionsSettings::EMPTY
        });
        proxy.set(second_set.clone()).await.expect("set completed").expect("set successful");
    }

    {
        let proxy = AccessibilityTest::connect_to_accessibilitymarker(&instance);
        // Ensure retrieved value matches set value
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(settings, expected_settings);
    }

    let _ = instance.destroy().await;
}
