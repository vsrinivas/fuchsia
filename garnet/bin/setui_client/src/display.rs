// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{DisplayProxy, DisplaySettings, LowLightMode, Theme};

pub async fn command(
    proxy: DisplayProxy,
    brightness: Option<f32>,
    auto_brightness: Option<bool>,
    auto_brightness_value: Option<f32>,
    light_sensor: bool,
    low_light_mode: Option<LowLightMode>,
    theme: Option<Theme>,
    screen_enabled: Option<bool>,
) -> WatchOrSetResult {
    // Light sensor Watch.
    if light_sensor {
        return Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch_light_sensor2(0.0))));
    }

    // Set call.
    let mut settings = DisplaySettings::EMPTY;
    settings.auto_brightness = auto_brightness;
    settings.adjusted_auto_brightness = auto_brightness_value;
    settings.brightness_value = brightness;
    settings.low_light_mode = low_light_mode;
    settings.screen_enabled = screen_enabled;

    if let Some(Theme { theme_type: Some(theme_type), .. }) = theme {
        settings.theme = Some(Theme { theme_type: Some(theme_type), ..Theme::EMPTY });
    }

    if settings == DisplaySettings::EMPTY {
        // No fields were set, interpret as a Watch.
        return Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())));
    } else {
        let mutate_result = proxy.set(settings.clone()).await?;
        match mutate_result {
            Ok(_) => {
                Ok(Either::Set(format!("Successfully set display settings to {:?}", settings)))
            }
            Err(err) => Ok(Either::Set(format!("{:?}", err))),
        }
    }
}
