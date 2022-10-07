// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_setui_display_args::{Field, GetArgs};
use fidl_fuchsia_settings::DisplayProxy;
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

pub async fn get(proxy: DisplayProxy, args: GetArgs) -> Result<()> {
    handle_mixed_result("DisplayGet", command(proxy, args).await).await
}

async fn command(proxy: DisplayProxy, args: GetArgs) -> WatchOrSetResult {
    // TODO(fxbug.dev/107621): Use FIDL wire format encoding and decoding once C++ supports it.
    // Add a Field option to return a certain field's value, otherwise, the whole display settings
    // will be returned.
    let res = proxy.watch().await;
    if let (Ok(settings), Some(field)) = (&res, args.field) {
        if field == Field::Brightness {
            return Ok(Either::Get(format!(
                "{:?}",
                settings.brightness_value.expect("brightness value present")
            )));
        } else if field == Field::Auto {
            return Ok(Either::Get(format!(
                "{:?}",
                settings.auto_brightness.expect("auto brightness present")
            )));
        }
    }

    Ok(Either::Get(format!("{:#?}", res)))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_display_proxy;
    use ffx_setui_display_args::SetArgs;
    use fidl_fuchsia_settings::{DisplayRequest, DisplaySettings};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get() {
        let expected_display = SetArgs {
            brightness: None,
            auto_brightness_level: None,
            auto_brightness: Some(false),
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };

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

        let get_args = GetArgs { field: Some(Field::Auto) };
        let response = get(proxy, get_args).await;
        assert!(response.is_ok());
    }

    #[should_panic]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_failure() {
        let expected_display = SetArgs {
            brightness: None,
            auto_brightness_level: None,
            auto_brightness: None,
            low_light_mode: None,
            theme: None,
            screen_enabled: None,
        };

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

        let get_args = GetArgs { field: Some(Field::Auto) };
        let _ = get(proxy, get_args).await;
    }
}
