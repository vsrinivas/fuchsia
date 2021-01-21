// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::{self, Either, WatchOrSetResult},
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
) -> WatchOrSetResult {
    let mut output = String::new();

    if let Some(auto_brightness_value) = auto_brightness {
        let mut settings = DisplaySettings::EMPTY;
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
        let mut settings = DisplaySettings::EMPTY;
        settings.brightness_value = Some(brightness_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set brightness to {}", brightness_value))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if light_sensor {
        return Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch_light_sensor2(0.0))));
    } else if let Some(mode) = low_light_mode {
        let mut settings = DisplaySettings::EMPTY;
        settings.low_light_mode = Some(mode);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!("Successfully set low_light_mode to {:?}", mode)),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(Theme { theme_type: Some(theme_type), .. }) = theme {
        let mut settings = DisplaySettings::EMPTY;
        settings.theme = Some(Theme { theme_type: Some(theme_type), ..Theme::EMPTY });

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!("Successfully set theme to {:?}", theme_type)),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(screen_enabled) = screen_enabled {
        let mut settings = DisplaySettings::EMPTY;
        settings.screen_enabled = Some(screen_enabled);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set screen_enabled to {:?}", screen_enabled))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        return Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())));
    }

    Ok(Either::Set(output))
}
