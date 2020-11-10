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
    screen_enabled: Option<bool>,
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
    } else if let Some(screen_enabled) = screen_enabled {
        let mut settings = DisplaySettings::empty();
        settings.screen_enabled = Some(screen_enabled);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set screen_enabled to {:?}", screen_enabled))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting_value = proxy.watch().await?;
        output.push_str(&format!("{:?}", setting_value));
    }

    Ok(output)
}
