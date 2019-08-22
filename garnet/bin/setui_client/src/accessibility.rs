// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: AccessibilityProxy,
    audio_description: Option<bool>,
    screen_reader: Option<bool>,
    color_inversion: Option<bool>,
    enable_magnification: Option<bool>,
    color_correction: Option<ColorBlindnessType>,
) -> Result<String, Error> {
    let mut output = String::new();

    if let Some(audio_description_value) = audio_description {
        let mut settings = AccessibilitySettings::empty();
        settings.audio_description = Some(audio_description_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set audio_description to {}",
                audio_description_value
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(screen_reader_value) = screen_reader {
        let mut settings = AccessibilitySettings::empty();
        settings.screen_reader = Some(screen_reader_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output
                .push_str(&format!("Successfully set screen_reader to {}", screen_reader_value)),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(color_inversion_value) = color_inversion {
        let mut settings = AccessibilitySettings::empty();
        settings.color_inversion = Some(color_inversion_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set color_inversion to {}",
                color_inversion_value
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(enable_magnification_value) = enable_magnification {
        let mut settings = AccessibilitySettings::empty();
        settings.enable_magnification = Some(enable_magnification_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set enable_magnification to {}",
                enable_magnification_value
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(color_correction_value) = color_correction {
        let mut settings = AccessibilitySettings::empty();
        settings.color_correction = Some(color_correction_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set color_correction to {}",
                describe_color_blindness_type(&color_correction_value)
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => {
                let setting_string = describe_accessibility_setting(&setting_value);
                output.push_str(&setting_string);
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    }

    Ok(output)
}

fn describe_accessibility_setting(accessibility_setting: &AccessibilitySettings) -> String {
    let mut output = String::new();

    output.push_str("Accessibility { ");

    if let Some(audio_description) = accessibility_setting.audio_description {
        output.push_str(&format!("audio_description: {} ", audio_description));
    }

    if let Some(screen_reader) = accessibility_setting.screen_reader {
        output.push_str(&format!("screen_reader: {}", screen_reader));
    }

    if let Some(color_inversion) = accessibility_setting.color_inversion {
        output.push_str(&format!("color_inversion: {}", color_inversion));
    }

    if let Some(enable_magnification) = accessibility_setting.enable_magnification {
        output.push_str(&format!("enable_magnification: {}", enable_magnification));
    }

    if let Some(color_correction) = accessibility_setting.color_correction {
        output.push_str(&format!(
            "color_correction: {} ",
            describe_color_blindness_type(&color_correction)
        ));
    }

    output.push_str("}");

    return output;
}

fn describe_color_blindness_type(color_blindness_type: &ColorBlindnessType) -> String {
    match color_blindness_type {
        fidl_fuchsia_settings::ColorBlindnessType::None => "none".into(),
        fidl_fuchsia_settings::ColorBlindnessType::Protanomaly => "protanomaly".into(),
        fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly => "deuteranomaly".into(),
        fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly => "tritanomaly".into(),
    }
}
