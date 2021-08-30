// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::accessibility::types::{AccessibilityInfo, ColorBlindnessType};
use crate::base::SettingType;
use crate::handler::device_storage::testing::InMemoryStorageFactory;
use crate::ingress::fidl::Interface;
use crate::tests::test_failure_utils::create_test_env_with_failures;
use crate::EnvironmentBuilder;
use fidl::Error::ClientChannelClosed;
use fidl_fuchsia_settings::*;
use fidl_fuchsia_ui_types::ColorRgba;
use fuchsia_zircon::Status;
use matches::assert_matches;
use std::sync::Arc;

const ENV_NAME: &str = "settings_service_accessibility_test_environment";

async fn create_test_accessibility_env(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> AccessibilityProxy {
    EnvironmentBuilder::new(storage_factory)
        .fidl_interfaces(&[Interface::Accessibility])
        .spawn_and_get_nested_environment(ENV_NAME)
        .await
        .unwrap()
        .connect_to_protocol::<AccessibilityMarker>()
        .unwrap()
}

// Creates an environment that will fail on a get request.
async fn create_a11y_test_env_with_failures(
    storage_factory: Arc<InMemoryStorageFactory>,
) -> AccessibilityProxy {
    create_test_env_with_failures(
        storage_factory,
        ENV_NAME,
        Interface::Accessibility,
        SettingType::Accessibility,
    )
    .await
    .connect_to_protocol::<AccessibilityMarker>()
    .unwrap()
}

#[fuchsia_async::run_until_stalled(test)]
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

    let expected_info = AccessibilityInfo {
        audio_description: expected_settings.audio_description,
        screen_reader: expected_settings.screen_reader,
        color_inversion: expected_settings.color_inversion,
        enable_magnification: expected_settings.enable_magnification,
        color_correction: Some(CHANGED_COLOR_BLINDNESS_TYPE),
        captions_settings: Some(CHANGED_CAPTION_SETTINGS.into()),
    };

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());
    let accessibility_proxy = create_test_accessibility_env(Arc::clone(&factory)).await;
    let store = factory.get_device_storage().await;

    // Fetch the initial value.
    let settings = accessibility_proxy.watch().await.expect("watch completed");
    assert_eq!(settings, initial_settings);

    // Set the various settings values.
    accessibility_proxy
        .set(expected_settings.clone())
        .await
        .expect("set completed")
        .expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(expected_info, retrieved_struct);

    // Verify the value we set is returned when watching.
    let settings = accessibility_proxy.watch().await.expect("watch completed");
    assert_eq!(settings, expected_settings);
}

#[fuchsia_async::run_until_stalled(test)]
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

    let expected_info = AccessibilityInfo {
        audio_description: None,
        screen_reader: None,
        color_inversion: None,
        enable_magnification: None,
        color_correction: None,
        captions_settings: Some(EXPECTED_CAPTIONS_SETTINGS.into()),
    };

    // Create and fetch a store from device storage so we can read stored value for testing.
    let factory = Arc::new(InMemoryStorageFactory::new());
    let accessibility_proxy = create_test_accessibility_env(Arc::clone(&factory)).await;
    let store = factory.get_device_storage().await;

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
    accessibility_proxy
        .set(first_set.clone())
        .await
        .expect("set completed")
        .expect("set successful");

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
    accessibility_proxy
        .set(second_set.clone())
        .await
        .expect("set completed")
        .expect("set successful");

    // Verify the value we set is persisted in DeviceStorage.
    let retrieved_struct = store.get().await;
    assert_eq!(expected_info, retrieved_struct);

    // Verify the value we set is returned when watching.
    let settings = accessibility_proxy.watch().await.expect("watch completed");
    assert_eq!(settings, expected_settings);
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_channel_failure_watch() {
    let accessibility_proxy =
        create_a11y_test_env_with_failures(Arc::new(InMemoryStorageFactory::new())).await;
    let result = accessibility_proxy.watch().await;
    assert_matches!(result, Err(ClientChannelClosed { status: Status::UNAVAILABLE, .. }));
}
