// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_display_args::{Display, SubCommandEnum};
use fidl_fuchsia_settings::DisplayProxy;

pub use utils;

mod get;
mod set;
mod watch;

#[ffx_plugin("setui", DisplayProxy = "core/setui_service:expose:fuchsia.settings.Display")]
pub async fn run_command(display_proxy: DisplayProxy, display: Display) -> Result<()> {
    match display.subcommand {
        SubCommandEnum::Set(args) => set::set(display_proxy, args).await,
        SubCommandEnum::Get(args) => get::get(display_proxy, args).await,
        SubCommandEnum::Watch(_) => watch::watch(display_proxy).await,
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::setup_fake_display_proxy;
    use ffx_setui_display_args::{Field, GetArgs, SetArgs};
    use fidl_fuchsia_settings::{DisplayRequest, DisplaySettings};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
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

        let display =
            Display { subcommand: SubCommandEnum::Get(GetArgs { field: Some(Field::Auto) }) };
        let response = run_command(proxy, display).await;
        assert!(response.is_ok());
    }
}
