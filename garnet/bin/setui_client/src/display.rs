// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_settings::{DisplayProxy, DisplaySettings, LowLightMode, Theme},
};

pub async fn command(
    proxy: DisplayProxy,
    brightness: Option<f32>,
    auto_brightness: Option<bool>,
    light_sensor: bool,
    low_light_mode: Option<LowLightMode>,
    theme: Option<Theme>,
) -> Result<String, Error> {
    let mut output = String::new();

    if let Some(auto_brightness_value) = auto_brightness {
        let mut settings = DisplaySettings::empty();
        settings.auto_brightness = Some(auto_brightness_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set auto_brightness to {}",
                auto_brightness_value
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(brightness_value) = brightness {
        let mut settings = DisplaySettings::empty();
        settings.brightness_value = Some(brightness_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set brightness to {}", brightness_value))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if light_sensor {
        let data = proxy.watch_light_sensor2(0.0).await?;
        output.push_str(&format!("{:?}", data));
    } else if let Some(mode) = low_light_mode {
        let mut settings = DisplaySettings::empty();
        settings.low_light_mode = Some(mode);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!("Successfully set low_light_mode to {:?}", mode)),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(Theme { theme_type: Some(theme_type), .. }) = theme {
        let mut settings = DisplaySettings::empty();
        settings.theme = Some(Theme { theme_type: Some(theme_type), ..Theme::empty() });

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!("Successfully set theme to {:?}", theme_type)),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting_value = proxy.watch().await?;
        let setting_string = describe_display_setting(&setting_value);
        output.push_str(&setting_string);
    }

    Ok(output)
}

fn describe_display_setting(display_setting: &DisplaySettings) -> String {
    let mut output = String::new();

    output.push_str("Display { ");

    if let Some(brightness) = display_setting.brightness_value {
        output.push_str(&format!("brightness_value: {} ", brightness))
    }

    if let Some(auto_brightness) = display_setting.auto_brightness {
        output.push_str(&format!("auto_brightness: {} ", auto_brightness))
    }

    if let Some(low_light_mode) = display_setting.low_light_mode {
        output.push_str(&format!("low_light_mode: {:?} ", low_light_mode))
    }

    if let Some(Theme { theme_type: Some(theme_type), .. }) = display_setting.theme {
        output.push_str(&format!("theme: {:?} ", theme_type))
    }

    output.push_str("}");

    return output;
}
