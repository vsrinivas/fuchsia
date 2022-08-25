// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use ffx_setui_display_args::SetArgs;
use fidl_fuchsia_settings::{DisplayProxy, DisplaySettings};
use utils::{self, handle_mixed_result, Either, WatchOrSetResult};

pub async fn set(proxy: DisplayProxy, args: SetArgs) -> Result<()> {
    handle_mixed_result("DisplaySet", command(proxy, DisplaySettings::from(args)).await).await
}

async fn command(proxy: DisplayProxy, settings: DisplaySettings) -> WatchOrSetResult {
    if settings == DisplaySettings::EMPTY {
        return Err(format_err!("At least one option is required."));
    }

    Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
        format!("{:?}", err)
    } else {
        format!("Successfully set Display to {:?}", SetArgs::from(settings))
    }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_display_proxy;
    use fidl_fuchsia_settings::{DisplayRequest, LowLightMode, Theme, ThemeMode, ThemeType};
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set() {
        let proxy = setup_fake_display_proxy(move |req| match req {
            DisplayRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            DisplayRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
            DisplayRequest::WatchLightSensor { .. } => {
                panic!("Unexpected call to watch light sensor");
            }
        });

        let display = SetArgs {
            brightness: None,
            auto_brightness_level: None,
            auto_brightness: Some(true),
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };
        let response = set(proxy, display).await;
        assert!(response.is_ok());
    }

    #[test_case(
        SetArgs {
            brightness: Some(0.5),
            auto_brightness_level: None,
            auto_brightness: Some(false),
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };
        "Test display set() output with non-empty input."
    )]
    #[test_case(
        SetArgs {
            brightness: None,
            auto_brightness_level: Some(0.8),
            auto_brightness: Some(true),
            low_light_mode: Some(LowLightMode::Enable),
            theme: Some(Theme {
                theme_type: Some(ThemeType::Dark),
                theme_mode: Some(ThemeMode::AUTO),
                ..Theme::EMPTY
            }),
            screen_enabled: Some(true),
        };
        "Test display set() output with a different non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_display_set_output(expected_display: SetArgs) -> Result<()> {
        let proxy = setup_fake_display_proxy(move |req| match req {
            DisplayRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            DisplayRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
            DisplayRequest::WatchLightSensor { .. } => {
                panic!("Unexpected call to watch light sensor");
            }
        });

        let output =
            utils::assert_set!(command(proxy, DisplaySettings::from(expected_display.clone())));
        assert_eq!(output, format!("Successfully set Display to {:?}", expected_display));
        Ok(())
    }
}
