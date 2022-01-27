// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use ffx_core::ffx_plugin;
use ffx_setui_privacy_args::Privacy;
use fidl_fuchsia_settings::{PrivacyProxy, PrivacySettings};
use utils::handle_mixed_result;
use utils::{self, Either, WatchOrSetResult};

#[ffx_plugin("setui", PrivacyProxy = "core/setui_service:expose:fuchsia.settings.Privacy")]
pub async fn run_command(privacy_proxy: PrivacyProxy, privacy: Privacy) -> Result<()> {
    handle_mixed_result("Privacy", command(privacy_proxy, privacy.user_data_sharing_consent).await)
        .await?;

    Ok(())
}

async fn command(proxy: PrivacyProxy, user_data_sharing_consent: Option<bool>) -> WatchOrSetResult {
    Ok(if let Some(user_data_sharing_consent_value) = user_data_sharing_consent {
        let mut settings = PrivacySettings::EMPTY;
        settings.user_data_sharing_consent = Some(user_data_sharing_consent_value);

        let mutate_result = proxy.set(settings).await?;
        Either::Set(match mutate_result {
            Ok(_) => format!(
                "Successfully set user_data_sharing_consent to {}",
                user_data_sharing_consent_value
            ),
            Err(err) => format!("{:?}", err),
        })
    } else {
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    })
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_settings::{PrivacyRequest, PrivacySettings};
    use futures::prelude::*;
    use test_case::test_case;

    #[test_case(
        true;
        "Test privacy set() output with user_data_sharing_consent as true."
    )]
    #[test_case(
        false;
        "Test privacy set() output with user_data_sharing_consent as false."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_privacy_set_output(expected_user_data_sharing_consent: bool) -> Result<()> {
        let proxy = setup_fake_privacy_proxy(move |req| match req {
            PrivacyRequest::Set { settings: _, responder } => {
                let _ = responder.send(&mut Ok(()));
            }
            PrivacyRequest::Watch { responder: _ } => {
                panic!("Unexpected call to watch");
            }
        });

        let output = utils::assert_set!(command(proxy, Some(expected_user_data_sharing_consent)));
        assert_eq!(
            output,
            format!(
                "Successfully set user_data_sharing_consent to {}",
                expected_user_data_sharing_consent
            )
        );
        Ok(())
    }

    #[test_case(
        None;
        "Test privacy watch() output with user_data_sharing_consent as None."
    )]
    #[test_case(
        Some(false);
        "Test privacy watch() output with user_data_sharing_consent as false."
    )]
    #[test_case(
        Some(true);
        "Test privacy watch() output with user_data_sharing_consent as true."
    )]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn validate_privacy_watch_output(
        expected_user_data_sharing_consent: Option<bool>,
    ) -> Result<()> {
        let proxy = setup_fake_privacy_proxy(move |req| match req {
            PrivacyRequest::Set { settings: _, responder: _ } => {
                panic!("Unexpected call to set");
            }
            PrivacyRequest::Watch { responder } => {
                let _ = responder.send(PrivacySettings {
                    user_data_sharing_consent: expected_user_data_sharing_consent,
                    ..PrivacySettings::EMPTY
                });
            }
        });

        let output = utils::assert_watch!(command(proxy, None));
        assert_eq!(
            output,
            format!(
                "{:#?}",
                PrivacySettings {
                    user_data_sharing_consent: expected_user_data_sharing_consent,
                    ..PrivacySettings::EMPTY
                }
            )
        );
        Ok(())
    }
}
