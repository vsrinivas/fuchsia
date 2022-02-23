// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_night_mode_args::NightMode;
use fidl_fuchsia_settings::{NightModeProxy, NightModeSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", NightModeProxy = "core/setui_service:expose:fuchsia.settings.NightMode")]
pub async fn run_command(night_mode_proxy: NightModeProxy, night_mode: NightMode) -> Result<()> {
    handle_mixed_result("NightMode", command(night_mode_proxy, night_mode.night_mode_enabled).await)
        .await
}

async fn command(proxy: NightModeProxy, night_mode_enabled: Option<bool>) -> WatchOrSetResult {
    let mut settings = NightModeSettings::EMPTY;
    settings.night_mode_enabled = night_mode_enabled;

    if settings == NightModeSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set night_mode_enabled to {:?}", night_mode_enabled)
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{NightModeRequest, NightModeSettings};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        const ENABLED: bool = true;

        let proxy = setup_fake_night_mode_proxy(move |req| match req {
            NightModeRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            NightModeRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let night_mode = NightMode { night_mode_enabled: Some(ENABLED) };
        let response = run_command(proxy, night_mode).await;
        assert!(response.is_ok());
    }

    #[test_case(
        true;
        "Test night mode set() output with night_mode_enabled as true."
    )]
    #[test_case(
        false;
        "Test night mode set() output with night_mode_enabled as false."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_night_mode_set_output(expected_night_mode_enabled: bool) -> Result<()> {
        let proxy = setup_fake_night_mode_proxy(move |req| match req {
            NightModeRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            NightModeRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, Some(expected_night_mode_enabled)));
        assert_eq!(
            output,
            format!(
                "Successfully set night_mode_enabled to {:?}",
                Some(expected_night_mode_enabled)
            )
        );
        Ok(())
    }

    #[test_case(
        None;
        "Test night mode watch() output with night_mode_enabled as None."
    )]
    #[test_case(
        Some(false);
        "Test night mode watch() output with night_mode_enabled as false."
    )]
    #[test_case(
        Some(true);
        "Test night mode watch() output with night_mode_enabled as true."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_night_mode_watch_output(
        expected_night_mode_enabled: Option<bool>,
    ) -> Result<()> {
        let proxy = setup_fake_night_mode_proxy(move |req| match req {
            NightModeRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            NightModeRequest::Watch { responder } => {
                let _ = responder.send(NightModeSettings {
                    night_mode_enabled: expected_night_mode_enabled,
                    ..NightModeSettings::EMPTY
                });
            }
        });

        let output = utils::assert_watch!(command(proxy, None));
        assert_eq!(
            output,
            format!(
                "{:#?}",
                NightModeSettings {
                    night_mode_enabled: expected_night_mode_enabled,
                    ..NightModeSettings::EMPTY
                }
            )
        );
        Ok(())
    }
}
