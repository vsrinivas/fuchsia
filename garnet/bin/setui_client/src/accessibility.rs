// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: AccessibilityProxy,
    debug_set_all: bool,
    audio_description: Option<bool>,
    screen_reader: Option<bool>,
    color_inversion: Option<bool>,
    enable_magnification: Option<bool>,
    color_correction: Option<ColorBlindnessType>,
) -> Result<String, Error> {
    let mut output = String::new();

    let mut settings = AccessibilitySettings::empty();
    settings.audio_description = audio_description;
    settings.screen_reader = screen_reader;
    settings.color_inversion = color_inversion;
    settings.enable_magnification = enable_magnification;
    settings.color_correction = color_correction;

    if debug_set_all {
        // Give everything a value.
        settings.audio_description = Some(true);
        settings.screen_reader = Some(true);
        settings.color_inversion = Some(true);
        settings.enable_magnification = Some(true);
        settings.color_correction = Some(ColorBlindnessType::Protanomaly);
    }

    if settings == AccessibilitySettings::empty() {
        // No values set, perform a get instead.
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => {
                let setting_string = describe_accessibility_setting(&setting_value);
                output.push_str(&setting_string);
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!("Successfully set AccessibilitySettings")),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    }

    Ok(output)
}

fn describe_accessibility_setting(accessibility_setting: &AccessibilitySettings) -> String {
    let mut output = String::new();

    output.push_str("Accessibility {\n");

    output
        .push_str(&format!("audio_description: {:?},\n", accessibility_setting.audio_description));

    output.push_str(&format!("screen_reader: {:?},\n", accessibility_setting.screen_reader));

    output.push_str(&format!("color_inversion: {:?},\n", accessibility_setting.color_inversion));

    output.push_str(&format!(
        "enable_magnification: {:?},\n",
        accessibility_setting.enable_magnification
    ));

    output.push_str(&format!(
        "color_correction: {:?},\n",
        describe_color_blindness_type(accessibility_setting.color_correction)
    ));

    output.push_str("}");

    return output;
}

fn describe_color_blindness_type(color_blindness_type: Option<ColorBlindnessType>) -> String {
    match color_blindness_type {
        Some(unpacked) => match unpacked {
            fidl_fuchsia_settings::ColorBlindnessType::None => "none".into(),
            fidl_fuchsia_settings::ColorBlindnessType::Protanomaly => "protanomaly".into(),
            fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly => "deuteranomaly".into(),
            fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly => "tritanomaly".into(),
        },
        None => "not set".into(),
    }
}
