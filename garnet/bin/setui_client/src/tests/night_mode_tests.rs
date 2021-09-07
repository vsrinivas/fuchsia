// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{NightModeMarker, NightModeRequest, NightModeSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::night_mode;

pub(crate) async fn validate_night_mode(
    expected_night_mode_enabled: Option<bool>,
) -> Result<(), Error> {
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

pub(crate) async fn validate_night_mode_set_output(
    expected_night_mode_enabled: bool,
) -> Result<(), Error> {
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

pub(crate) async fn validate_night_mode_watch_output(
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
