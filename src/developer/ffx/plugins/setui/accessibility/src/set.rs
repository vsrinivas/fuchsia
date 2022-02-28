// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use ffx_setui_accessibility_args::SetArgs;
use fidl_fuchsia_settings::{AccessibilityProxy, AccessibilitySettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn set(accessibility_proxy: AccessibilityProxy, args: SetArgs) -> Result<()> {
    handle_mixed_result("AccessibilitySet", command(accessibility_proxy, args).await).await
}

async fn command(proxy: AccessibilityProxy, options: SetArgs) -> WatchOrSetResult {
    let mut settings = AccessibilitySettings::EMPTY;
    settings.audio_description = options.audio_description;
    settings.screen_reader = options.screen_reader;
    settings.color_inversion = options.color_inversion;
    settings.enable_magnification = options.enable_magnification;
    settings.color_correction = options.color_correction;

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
    use fidl_fuchsia_settings::{AccessibilityRequest, ColorBlindnessType};
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_set() {
        const TRUE: bool = true;
        let proxy = setup_fake_accessibility_proxy(move |req| match req {
            AccessibilityRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AccessibilityRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let args = SetArgs {
            audio_description: Some(TRUE),
            screen_reader: None,
            color_inversion: None,
            enable_magnification: None,
            color_correction: None,
        };
        let response = set(proxy, args).await;
        assert!(response.is_ok());
    }

    #[test_case(
        SetArgs {
            audio_description: Some(true),
            screen_reader: Some(false),
            color_inversion: Some(false),
            enable_magnification: None,
            color_correction: Some(ColorBlindnessType::Protanomaly),
        };
        "Test set other settings."
    )]
    #[test_case(
        SetArgs {
            audio_description: Some(false),
            screen_reader: Some(true),
            color_inversion: None,
            enable_magnification: Some(false),
            color_correction: Some(ColorBlindnessType::Deuteranomaly),
        };
        "Test set other settings with different inputs."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_accessibility_set(expected_set: SetArgs) -> Result<()> {
        let set_clone = expected_set.clone();
        let proxy = setup_fake_accessibility_proxy(move |req| match req {
            AccessibilityRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            AccessibilityRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, set_clone));
        assert_eq!(output, format!("Successfully set AccessibilitySettings"));
        Ok(())
    }
}
