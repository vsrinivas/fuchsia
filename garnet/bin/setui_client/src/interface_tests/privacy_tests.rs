// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::privacy;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{PrivacyMarker, PrivacyRequest, PrivacySettings};

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn validate_privacy(expected_user_data_sharing_consent: Option<bool>) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings, responder, } => {
            if let (Some(user_data_sharing_consent), Some(expected_user_data_sharing_consent_value)) =
                (settings.user_data_sharing_consent, expected_user_data_sharing_consent) {
                assert_eq!(user_data_sharing_consent, expected_user_data_sharing_consent_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: Some(false),
                ..PrivacySettings::EMPTY
            })?;
        }
    );

    let privacy_service = env
        .connect_to_protocol::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    assert_successful!(privacy::command(privacy_service, expected_user_data_sharing_consent));
    Ok(())
}

async fn validate_privacy_set_output(
    expected_user_data_sharing_consent: bool,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: Some(expected_user_data_sharing_consent),
                ..PrivacySettings::EMPTY
            })?;
        }
    );

    let privacy_service = env
        .connect_to_protocol::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    let output =
        assert_set!(privacy::command(privacy_service, Some(expected_user_data_sharing_consent)));
    assert_eq!(
        output,
        format!(
            "Successfully set user_data_sharing_consent to {}",
            expected_user_data_sharing_consent
        )
    );
    Ok(())
}

async fn validate_privacy_watch_output(
    expected_user_data_sharing_consent: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::Privacy, PrivacyRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        PrivacyRequest::Watch { responder } => {
            responder.send(PrivacySettings {
                user_data_sharing_consent: expected_user_data_sharing_consent,
                ..PrivacySettings::EMPTY
            })?;
        }
    );

    let privacy_service = env
        .connect_to_protocol::<PrivacyMarker>()
        .context("Failed to connect to privacy service")?;

    let output = assert_watch!(privacy::command(privacy_service, None));
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

#[fuchsia_async::run_until_stalled(test)]
async fn test_privacy() -> Result<(), Error> {
    println!("privacy service tests");
    println!("  client calls privacy watch");
    validate_privacy(None).await?;

    println!("  client calls set user_data_sharing_consent");
    validate_privacy(Some(true)).await?;

    println!("  set() output");
    validate_privacy_set_output(true).await?;
    validate_privacy_set_output(false).await?;

    println!("  watch() output");
    validate_privacy_watch_output(None).await?;
    validate_privacy_watch_output(Some(true)).await?;
    validate_privacy_watch_output(Some(false)).await?;

    Ok(())
}
