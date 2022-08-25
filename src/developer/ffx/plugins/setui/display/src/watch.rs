// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use fidl_fuchsia_settings::DisplayProxy;
use utils::{self, handle_mixed_result, Either, WatchOrSetResult};

pub async fn watch(proxy: DisplayProxy) -> Result<()> {
    handle_mixed_result("DisplayWatch", command(proxy).await).await
}

async fn command(proxy: DisplayProxy) -> WatchOrSetResult {
    Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_display_proxy;
    use ffx_setui_display_args::SetArgs;
    use fidl_fuchsia_settings::{DisplayRequest, DisplaySettings};
    use test_case::test_case;

    #[test_case(
        SetArgs {
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
        SetArgs {
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
    async fn validate_display_watch_output(expected_display: SetArgs) -> Result<()> {
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

        let output = utils::assert_watch!(command(proxy));
        assert_eq!(output, format!("{:#?}", DisplaySettings::from(expected_display_clone)));
        Ok(())
    }
}
