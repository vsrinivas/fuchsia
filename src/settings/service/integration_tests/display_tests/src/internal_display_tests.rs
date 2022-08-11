// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use common::DisplayTest;
use fidl_fuchsia_settings::{
    DisplaySettings, Error as FidlError, LowLightMode as FidlLowLightMode, Theme as FidlTheme,
    ThemeMode as FidlThemeMode, ThemeType as FidlThemeType,
};
use utils::async_property_test;

const STARTING_BRIGHTNESS: f32 = 0.5;
const CHANGED_BRIGHTNESS: f32 = 0.8;
const AUTO_BRIGHTNESS_LEVEL: f32 = 0.9;

// Tests that the FIDL calls for manual brightness result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_manual_brightness() {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.brightness_value, Some(STARTING_BRIGHTNESS));

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.brightness_value = Some(CHANGED_BRIGHTNESS);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.brightness_value, Some(CHANGED_BRIGHTNESS));

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for auto brightness result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_auto_brightness() {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.auto_brightness = Some(true);
    display_settings.adjusted_auto_brightness = Some(AUTO_BRIGHTNESS_LEVEL);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.auto_brightness, Some(true));
    assert_eq!(settings.adjusted_auto_brightness, Some(AUTO_BRIGHTNESS_LEVEL));

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for light mode result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_light_mode() {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    // Test that if display is enabled, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::Enable);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::Enable));

    // Test that if display is disabled, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::Disable);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::Disable));

    // Test that if display is disabled immediately, it is reflected.
    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.low_light_mode = Some(FidlLowLightMode::DisableImmediately);
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");

    assert_eq!(settings.low_light_mode, Some(FidlLowLightMode::DisableImmediately));

    let _ = instance.destroy().await;
}

// Tests for display theme.
#[fuchsia::test]
async fn test_theme_type_light() {
    let incoming_theme =
        Some(FidlTheme { theme_type: Some(FidlThemeType::Light), ..FidlTheme::EMPTY });
    let expected_theme = incoming_theme.clone();

    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.theme = incoming_theme;
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_theme_type_light_theme_mode_empty() {
    let incoming_theme = Some(FidlTheme {
        theme_type: Some(FidlThemeType::Light),
        theme_mode: Some(FidlThemeMode::empty()),
        ..FidlTheme::EMPTY
    });
    // theme_mode of 0x00 should be replaced with theme_mode absent.
    let expected_theme =
        Some(FidlTheme { theme_type: Some(FidlThemeType::Light), ..FidlTheme::EMPTY });

    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.theme = incoming_theme;
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_no_theme_set() {
    let expected_theme = Some(FidlTheme {
        theme_type: Some(FidlThemeType::Light),
        theme_mode: Some(FidlThemeMode::AUTO),
        ..FidlTheme::EMPTY
    });
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_theme_mode_auto() {
    let incoming_theme =
        Some(FidlTheme { theme_mode: Some(FidlThemeMode::AUTO), ..FidlTheme::EMPTY });
    let expected_theme = Some(FidlTheme {
        theme_type: Some(FidlThemeType::Light),
        ..incoming_theme.clone().unwrap()
    });

    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.theme = incoming_theme;
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_theme_mode_auto_and_type_light() {
    let incoming_theme = Some(FidlTheme {
        theme_mode: Some(FidlThemeMode::AUTO),
        theme_type: Some(FidlThemeType::Light),
        ..FidlTheme::EMPTY
    });
    let expected_theme = incoming_theme.clone();

    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut display_settings = DisplaySettings::EMPTY;
    display_settings.theme = incoming_theme;
    proxy.set(display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

#[fuchsia::test]
async fn test_theme_mode_auto_preserves_previous_type() {
    let first_incoming_theme =
        Some(FidlTheme { theme_type: Some(FidlThemeType::Light), ..FidlTheme::EMPTY });
    let second_incoming_theme =
        Some(FidlTheme { theme_mode: Some(FidlThemeMode::AUTO), ..FidlTheme::EMPTY });
    let expected_theme = Some(FidlTheme {
        theme_mode: Some(FidlThemeMode::AUTO),
        theme_type: Some(FidlThemeType::Light),
        ..FidlTheme::EMPTY
    });

    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let mut first_display_settings = DisplaySettings::EMPTY;
    first_display_settings.theme = first_incoming_theme;
    proxy.set(first_display_settings).await.expect("set completed").expect("set successful");

    let mut second_display_settings = DisplaySettings::EMPTY;
    second_display_settings.theme = second_incoming_theme;
    proxy.set(second_display_settings).await.expect("set completed").expect("set successful");

    let settings = proxy.watch().await.expect("watch completed");
    assert_eq!(settings.theme, expected_theme);

    let _ = instance.destroy().await;
}

// Tests that the FIDL calls for screen enabled result in appropriate
// commands sent to the service.
#[fuchsia::test]
async fn test_screen_enabled() {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    DisplayTest::test_screen_enabled(proxy).await;

    let _ = instance.destroy().await;
}

// Validate that we can set multiple fields at once.
#[fuchsia::test]
async fn test_set_multiple_fields_success() {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);
    let settings = DisplaySettings {
        auto_brightness: Some(false),
        brightness_value: Some(0.5),
        adjusted_auto_brightness: Some(0.5),
        low_light_mode: Some(FidlLowLightMode::Enable),
        screen_enabled: Some(true),
        theme: Some(FidlTheme {
            theme_type: Some(FidlThemeType::Dark),
            theme_mode: None,
            ..FidlTheme::EMPTY
        }),
        ..DisplaySettings::EMPTY
    };
    proxy.set(settings.clone()).await.expect("set completed").expect("set successful");

    let settings_result = proxy.watch().await.expect("watch completed");
    assert_eq!(settings_result, settings);

    let _ = instance.destroy().await;
}

async_property_test!(test_set_multiple_fields_brightness => [
    success_case_1(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), Some(false), Some(true), true),
    success_case_2(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), None, Some(true), true),
    success_case_3(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), Some(false), None, true),
    success_case_4(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), None, None, true),
    success_case_5(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), Some(true), Some(true), true),
    success_case_6(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), Some(true), Some(false), true),
    success_case_7(Some(0.7), Some(AUTO_BRIGHTNESS_LEVEL), Some(false), Some(false), true),
]);
async fn test_set_multiple_fields_brightness(
    brightness_value: Option<f32>,
    adjusted_auto_brightness: Option<f32>,
    auto_brightness: Option<bool>,
    screen_enabled: Option<bool>,
    expect_success: bool,
) {
    let instance = DisplayTest::create_realm().await.expect("setting up test realm");
    let proxy = DisplayTest::connect_to_displaymarker(&instance);

    let settings = DisplaySettings {
        auto_brightness,
        brightness_value,
        screen_enabled,
        adjusted_auto_brightness,
        ..DisplaySettings::EMPTY
    };
    let result = proxy.set(settings).await.expect("set completed");
    if expect_success {
        assert!(result.is_ok());
        let settings = proxy.watch().await.expect("watch completed");
        assert_eq!(
            settings,
            DisplaySettings {
                auto_brightness: if auto_brightness.is_none() {
                    Some(false)
                } else {
                    auto_brightness
                },
                adjusted_auto_brightness: if adjusted_auto_brightness.is_none() {
                    Some(0.5)
                } else {
                    adjusted_auto_brightness
                },
                brightness_value: if brightness_value.is_none() {
                    Some(0.5)
                } else {
                    brightness_value
                },
                screen_enabled: if screen_enabled.is_none() { Some(true) } else { screen_enabled },
                // Default values untouched.
                low_light_mode: Some(FidlLowLightMode::Disable),
                theme: Some(FidlTheme {
                    theme_type: Some(FidlThemeType::Light),
                    theme_mode: Some(FidlThemeMode::AUTO),
                    ..FidlTheme::EMPTY
                }),
                ..DisplaySettings::EMPTY
            }
        );
    } else {
        assert_eq!(result, Err(FidlError::Failed));
    }

    let _ = instance.destroy().await;
}
