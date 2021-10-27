// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use crate::{Accessibility, CaptionCommands};
use fidl_fuchsia_settings::{
    AccessibilityProxy, AccessibilitySettings, CaptionFontStyle, CaptionsSettings,
};

pub async fn command(proxy: AccessibilityProxy, options: Accessibility) -> WatchOrSetResult {
    let mut settings = AccessibilitySettings::EMPTY;
    settings.audio_description = options.audio_description;
    settings.screen_reader = options.screen_reader;
    settings.color_inversion = options.color_inversion;
    settings.enable_magnification = options.enable_magnification;
    settings.color_correction = options.color_correction;

    if let Some(caption_settings_enum) = options.caption_options {
        let CaptionCommands::CaptionOptions(input) = caption_settings_enum;

        let font_style = CaptionFontStyle {
            family: input.font_family,
            color: input.font_color,
            relative_size: input.relative_size,
            char_edge_style: input.char_edge_style,
            ..CaptionFontStyle::EMPTY
        };

        let captions_settings = CaptionsSettings {
            for_media: input.for_media,
            for_tts: input.for_tts,
            window_color: input.window_color,
            background_color: input.background_color,
            font_style: Some(font_style),
            ..CaptionsSettings::EMPTY
        };

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
