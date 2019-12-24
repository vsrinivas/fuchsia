// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{AccessibilityOptions, CaptionCommands};
use anyhow::Error;
use fidl_fuchsia_settings::{
    AccessibilityProxy, AccessibilitySettings, CaptionFontStyle, CaptionsSettings,
};

pub async fn command(
    proxy: AccessibilityProxy,
    options: AccessibilityOptions,
) -> Result<String, Error> {
    let mut settings = AccessibilitySettings::empty();
    settings.audio_description = options.audio_description;
    settings.screen_reader = options.screen_reader;
    settings.color_inversion = options.color_inversion;
    settings.enable_magnification = options.enable_magnification;
    settings.color_correction = options.color_correction;

    if let Some(caption_settings_enum) = options.caption_options {
        let CaptionCommands::CaptionOptions(input) = caption_settings_enum;

        let style = input.style;
        let mut font_style = CaptionFontStyle::empty();
        font_style.family = style.font_family;
        font_style.color = style.font_color;
        font_style.relative_size = style.relative_size;
        font_style.char_edge_style = style.char_edge_style;

        let mut captions_settings = CaptionsSettings::empty();
        captions_settings.for_media = input.for_media;
        captions_settings.for_tts = input.for_tts;
        captions_settings.window_color = input.window_color;
        captions_settings.background_color = input.background_color;
        captions_settings.font_style = Some(font_style);

        settings.captions_settings = Some(captions_settings);
    }

    if settings == AccessibilitySettings::empty() {
        // No values set, perform a get instead.
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => Ok(format!("{:#?}", setting_value)),
            Err(err) => Ok(format!("{:#?}", err)),
        }
    } else {
        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => Ok(format!("Successfully set AccessibilitySettings")),
            Err(err) => Ok(format!("{:#?}", err)),
        }
    }
}
