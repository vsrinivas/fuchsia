// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use ffx_setui_accessibility_args::CaptionArgs;
use fidl_fuchsia_settings::{
    AccessibilityProxy, AccessibilitySettings, CaptionFontStyle, CaptionsSettings,
};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn add_caption(accessibility_proxy: AccessibilityProxy, args: CaptionArgs) -> Result<()> {
    handle_mixed_result("AccessibilityAddCaption", command(accessibility_proxy, args).await).await
}

async fn command(proxy: AccessibilityProxy, input: CaptionArgs) -> WatchOrSetResult {
    let mut settings = AccessibilitySettings::EMPTY;

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
        font_style: if font_style != CaptionFontStyle::EMPTY { Some(font_style) } else { None },
        ..CaptionsSettings::EMPTY
    };

    if captions_settings != CaptionsSettings::EMPTY {
        settings.captions_settings = Some(captions_settings);
    }

    if settings == AccessibilitySettings::EMPTY {
        return Err(format_err!("At least one option is required."));
    }

    Ok(Either::Set(if let Err(err) = proxy.set(settings).await? {
        format!("{:?}", err)
    } else {
        format!("Successfully set AccessibilitySettings")
    }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_accessibility_proxy;
    use fidl_fuchsia_settings::{AccessibilityRequest, CaptionFontFamily, EdgeStyle};
    use fidl_fuchsia_ui_types::ColorRgba;
    use test_case::test_case;

    const TEST_COLOR: ColorRgba = ColorRgba { red: 238.0, green: 23.0, blue: 128.0, alpha: 255.0 };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_caption() {
        const TRUE: bool = true;
        let proxy = setup_fake_accessibility_proxy(move |req| match req {
            AccessibilityRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AccessibilityRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let args = CaptionArgs {
            for_media: Some(TRUE),
            for_tts: None,
            window_color: None,
            background_color: None,
            font_family: None,
            font_color: None,
            relative_size: None,
            char_edge_style: None,
        };
        let response = add_caption(proxy, args).await;
        assert!(response.is_ok());
    }

    #[test_case(
        CaptionArgs {
            for_media: Some(true),
            for_tts: None,
            window_color: Some(TEST_COLOR),
            background_color: None,
            font_family: Some(CaptionFontFamily::Cursive),
            font_color: Some(TEST_COLOR),
            relative_size: None,
            char_edge_style: Some(EdgeStyle::Raised),
        };
        "Test add caption settings."
    )]
    #[test_case(
        CaptionArgs {
            for_media: Some(false),
            for_tts: Some(false),
            window_color: None,
            background_color: Some(TEST_COLOR),
            font_family: Some(CaptionFontFamily::ProportionalSansSerif),
            font_color: None,
            relative_size: Some(1.0),
            char_edge_style: Some(EdgeStyle::Depressed),
        };
        "Test add caption settings with different inputs."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_accessibility_add_caption(expected_add: CaptionArgs) -> Result<()> {
        let add_clone = expected_add.clone();
        let proxy = setup_fake_accessibility_proxy(move |req| match req {
            AccessibilityRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AccessibilityRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, add_clone));
        assert_eq!(output, format!("Successfully set AccessibilitySettings"));
        Ok(())
    }
}
