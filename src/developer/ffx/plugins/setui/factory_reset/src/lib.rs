// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_factory_reset_args::FactoryReset;
use fidl_fuchsia_settings::{FactoryResetProxy, FactoryResetSettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin(
    "setui",
    FactoryResetProxy = "core/setui_service:expose:fuchsia.settings.FactoryReset"
)]
pub async fn run_command(
    factory_reset_proxy: FactoryResetProxy,
    factory_reset: FactoryReset,
) -> Result<()> {
    handle_mixed_result(
        "FactoryReset",
        command(factory_reset_proxy, factory_reset.is_local_reset_allowed).await,
    )
    .await
}

async fn command(
    proxy: FactoryResetProxy,
    is_local_reset_allowed: Option<bool>,
) -> WatchOrSetResult {
    let mut settings = FactoryResetSettings::EMPTY;
    settings.is_local_reset_allowed = is_local_reset_allowed;

    if settings == FactoryResetSettings::EMPTY {
        Ok(Either::Watch(utils::watch_to_stream(proxy, |p| p.watch())))
    } else {
        Ok(Either::Set(if let Err(err) = proxy.set(settings.clone()).await? {
            format!("{:?}", err)
        } else {
            format!("Successfully set factory_reset to {:?}", settings)
        }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{FactoryResetRequest, FactoryResetSettings};
    use futures::prelude::*;
    use test_case::test_case;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_command() {
        const ALLOWED: bool = true;

        let proxy = setup_fake_factory_reset_proxy(move |req| match req {
            FactoryResetRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            FactoryResetRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let factory_reset = FactoryReset { is_local_reset_allowed: Some(ALLOWED) };
        let response = run_command(proxy, factory_reset).await;
        assert!(response.is_ok());
    }

    #[test_case(
        true;
        "Test factory reset set() output with is_local_reset_allowed as true."
    )]
    #[test_case(
        false;
        "Test factory reset set() output with is_local_reset_allowed as false."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_factory_reset_set_output(
        expected_is_local_reset_allowed: bool,
    ) -> Result<()> {
        let proxy = setup_fake_factory_reset_proxy(move |req| match req {
            FactoryResetRequest::Set { responder, .. } => {
                let _ = responder.send(&mut Ok(()));
            }
            FactoryResetRequest::Watch { .. } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, Some(expected_is_local_reset_allowed)));
        assert_eq!(
            output,
            format!(
                "Successfully set factory_reset to {:?}",
                FactoryResetSettings {
                    is_local_reset_allowed: Some(expected_is_local_reset_allowed),
                    ..FactoryResetSettings::EMPTY
                }
            )
        );
        Ok(())
    }

    #[test_case(
        None;
        "Test factory reset watch() output with is_local_reset_allowed as None."
    )]
    #[test_case(
        Some(false);
        "Test factory reset watch() output with is_local_reset_allowed as false."
    )]
    #[test_case(
        Some(true);
        "Test factory reset watch() output with is_local_reset_allowed as true."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_factory_reset_watch_output(
        expected_is_local_reset_allowed: Option<bool>,
    ) -> Result<()> {
        let proxy = setup_fake_factory_reset_proxy(move |req| match req {
            FactoryResetRequest::Set { .. } => {
                panic!("Unexpected call to set");
            }
            FactoryResetRequest::Watch { responder } => {
                let _ = responder.send(FactoryResetSettings {
                    is_local_reset_allowed: expected_is_local_reset_allowed,
                    ..FactoryResetSettings::EMPTY
                });
            }
        });

        let output = utils::assert_watch!(command(proxy, None));
        assert_eq!(
            output,
            format!(
                "{:#?}",
                FactoryResetSettings {
                    is_local_reset_allowed: expected_is_local_reset_allowed,
                    ..FactoryResetSettings::EMPTY
                }
            )
        );
        Ok(())
    }
}
