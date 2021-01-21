// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use crate::{AccessibilityOptions, CaptionCommands};
use fidl_fuchsia_settings::{
    AccessibilityProxy, AccessibilitySettings, CaptionFontStyle, CaptionsSettings,
};

pub async fn command(proxy: AccessibilityProxy, options: AccessibilityOptions) -> WatchOrSetResult {
    let mut settings = AccessibilitySettings::EMPTY;
    settings.audio_description = options.audio_description;
    settings.screen_reader = options.screen_reader;
    settings.color_inversion = options.color_inversion;
    settings.enable_magnification = options.enable_magnification;
    settings.color_correction = options.color_correction;

    if let Some(caption_settings_enum) = options.caption_options {
        let CaptionCommands::CaptionOptions(input) = caption_settings_enum;

        let style = input.style;
        let mut font_style = CaptionFontStyle::EMPTY;
        font_style.family = style.font_family;
        font_style.color = style.font_color;
        font_style.relative_size = style.relative_size;
        font_style.char_edge_style = style.char_edge_style;

        let mut captions_settings = CaptionsSettings::EMPTY;
        captions_settings.for_media = input.for_media;
        captions_settings.for_tts = input.for_tts;
        captions_settings.window_color = input.window_color;
        captions_settings.background_color = input.background_color;
        captions_settings.font_style = Some(font_style);

        settings.captions_settings = Some(captions_settings);
    }

    Ok(if settings == AccessibilitySettings::EMPTY {
        // No values set, perform a watch loop instead.
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    } else {
        let mutate_result = proxy.set(settings).await?;
        Either::Set(match mutate_result {
            Ok(_) => format!("Successfully set AccessibilitySettings"),
            Err(err) => format!("{:#?}", err),
        })
    })
}
