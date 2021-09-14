// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use crate::night_mode;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{NightModeMarker, NightModeRequest, NightModeSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn validate_night_mode(expected_night_mode_enabled: Option<bool>) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings, responder, } => {
            if let (Some(night_mode_enabled), Some(expected_night_mode_enabled_value)) =
                (settings.night_mode_enabled, expected_night_mode_enabled) {
                assert_eq!(night_mode_enabled, expected_night_mode_enabled_value);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: Some(false),
                ..NightModeSettings::EMPTY
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_protocol::<NightModeMarker>()
        .context("Failed to connect to night mode service")?;

    assert_successful!(night_mode::command(night_mode_service, expected_night_mode_enabled));
    Ok(())
}

async fn validate_night_mode_set_output(expected_night_mode_enabled: bool) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: Some(expected_night_mode_enabled),
                ..NightModeSettings::EMPTY
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_protocol::<NightModeMarker>()
        .context("Failed to connect to night mode service")?;

    let output =
        assert_set!(night_mode::command(night_mode_service, Some(expected_night_mode_enabled)));
    assert_eq!(
        output,
        format!("Successfully set night_mode_enabled to {}", expected_night_mode_enabled)
    );
    Ok(())
}

async fn validate_night_mode_watch_output(
    expected_night_mode_enabled: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(
        Services::NightMode, NightModeRequest::Set { settings: _, responder, } => {
            responder.send(&mut Ok(()))?;
        },
        NightModeRequest::Watch { responder } => {
            responder.send(NightModeSettings {
                night_mode_enabled: expected_night_mode_enabled,
                ..NightModeSettings::EMPTY
            })?;
        }
    );

    let night_mode_service = env
        .connect_to_protocol::<NightModeMarker>()
        .context("Failed to connect to night_mode service")?;

    let output = assert_watch!(night_mode::command(night_mode_service, None));
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

#[fuchsia_async::run_until_stalled(test)]
async fn test_night_mode() -> Result<(), Error> {
    println!("night mode service tests");
    println!("  client calls night mode watch");
    validate_night_mode(None).await?;

    println!("  client calls set night_mode_enabled");
    validate_night_mode(Some(true)).await?;

    println!("  set() output");
    validate_night_mode_set_output(true).await?;
    validate_night_mode_set_output(false).await?;

    println!("  watch() output");
    validate_night_mode_watch_output(None).await?;
    validate_night_mode_watch_output(Some(true)).await?;
    validate_night_mode_watch_output(Some(false)).await?;

    Ok(())
}
