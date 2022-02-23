// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_do_not_disturb_args::DoNotDisturb;
use fidl_fuchsia_settings::{DoNotDisturbProxy, DoNotDisturbSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin(
    "setui",
    DoNotDisturbProxy = "core/setui_service:expose:fuchsia.settings.DoNotDisturb"
)]
pub async fn run_command(
    do_not_disturb_proxy: DoNotDisturbProxy,
    do_not_disturb: DoNotDisturb,
) -> Result<()> {
    handle_mixed_result("DoNotDisturb", command(do_not_disturb_proxy, do_not_disturb).await).await
}

async fn command(proxy: DoNotDisturbProxy, do_not_disturb: DoNotDisturb) -> WatchOrSetResult {
    let settings = DoNotDisturbSettings::from(do_not_disturb);

    if settings == DoNotDisturbSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set DoNotDisturb to {:?}", do_not_disturb)
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{DoNotDisturbRequest, DoNotDisturbSettings};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        const USER: bool = true;
        const NIGHT_MODE: bool = false;

        let proxy = setup_fake_do_not_disturb_proxy(move |req| match req {
            DoNotDisturbRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            DoNotDisturbRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let dnd = DoNotDisturb { user_dnd: Some(USER), night_mode_dnd: Some(NIGHT_MODE) };
        let response = run_command(proxy, dnd).await;
        assert!(response.is_ok());
    }

    #[test_case(
        DoNotDisturb {
            user_dnd: Some(false),
            night_mode_dnd: Some(false),
        };
        "Test do not disturb set() output with both user_dnd and night_mode_dnd as false."
    )]
    #[test_case(
        DoNotDisturb {
            user_dnd: Some(true),
            night_mode_dnd: Some(false),
        };
        "Test do not disturb set() output with user_dnd as true and night_mode_dnd as false."
    )]
    #[test_case(
        DoNotDisturb {
            user_dnd: Some(false),
            night_mode_dnd: Some(true),
        };
        "Test do not disturb set() output with user_dnd and night_mode_dnd as true."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_do_not_disturb_set_output(
        expected_do_not_disturb: DoNotDisturb,
    ) -> Result<()> {
        let proxy = setup_fake_do_not_disturb_proxy(move |req| match req {
            DoNotDisturbRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            DoNotDisturbRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, expected_do_not_disturb));
        assert_eq!(
            output,
            format!("Successfully set DoNotDisturb to {:?}", expected_do_not_disturb)
        );
        Ok(())
    }

    #[test_case(
        DoNotDisturb {
            user_dnd: None,
            night_mode_dnd: None,
        };
        "Test do not disturb watch() output with empty DoNotDisturb."
    )]
    #[test_case(
        DoNotDisturb {
            user_dnd: None,
            night_mode_dnd: Some(false),
        };
        "Test do not disturb watch() output with non-empty DoNotDisturb."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_do_not_disturb_watch_output(
        expected_do_not_disturb: DoNotDisturb,
    ) -> Result<()> {
        let proxy = setup_fake_do_not_disturb_proxy(move |req| match req {
            DoNotDisturbRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            DoNotDisturbRequest::Watch { responder } => {
                let _ = responder.send(DoNotDisturbSettings::from(expected_do_not_disturb));
            }
        });

        let output = utils::assert_watch!(command(
            proxy,
            DoNotDisturb { user_dnd: None, night_mode_dnd: None }
        ));
        assert_eq!(output, format!("{:#?}", DoNotDisturbSettings::from(expected_do_not_disturb)));
        Ok(())
    }
}
