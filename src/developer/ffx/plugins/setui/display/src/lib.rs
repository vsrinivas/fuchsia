// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_display_args::Display;
use fidl_fuchsia_settings::{DisplayProxy, DisplaySettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", DisplayProxy = "core/setui_service:expose:fuchsia.settings.Display")]
pub async fn run_command(display_proxy: DisplayProxy, display: Display) -> Result<()> {
    handle_mixed_result("Display", command(display_proxy, DisplaySettings::from(display)).await)
        .await
}

async fn command(proxy: DisplayProxy, settings: DisplaySettings) -> WatchOrSetResult {
    if settings == DisplaySettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set Display to {:?}", Display::from(settings))
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{DisplayRequest, LowLightMode, Theme, ThemeMode, ThemeType};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
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

        let display = Display {
            brightness: None,
            auto_brightness_level: None,
            auto_brightness: Some(true),
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };
        let response = run_command(proxy, display).await;
        assert!(response.is_ok());
    }

    #[test_case(
        Display {
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
        Display {
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
    async fn validate_display_set_output(expected_display: Display) -> Result<()> {
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

    #[test_case(
        Display {
            brightness: None,
            auto_brightness_level: None,
            auto_brightness: None,
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };
        "Test display watch() output with empty input."
    )]
    #[test_case(
        Display {
            brightness: Some(0.5),
            auto_brightness_level: None,
            auto_brightness: Some(false),
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };
        "Test display watch() output with non-empty input."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_display_watch_output(expected_display: Display) -> Result<()> {
        let expected_display_clone = expected_display.clone();
        let proxy = setup_fake_display_proxy(move |req| match req {
            DisplayRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            DisplayRequest::Watch { responder } => {
                let _ = responder.send(DisplaySettings::from(expected_display.clone()));
            }
            DisplayRequest::WatchLightSensor { .. } => {
                panic!("Unexpected call to watch light sensor");
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            DisplaySettings::from(Display {
                brightness: None,
                auto_brightness_level: None,
                auto_brightness: None,
                low_light_mode: None,
                theme: None,
                screen_enabled: None,
            })
        ));
        assert_eq!(output, format!("{:#?}", DisplaySettings::from(expected_display_clone)));
        Ok(())
    }
}
