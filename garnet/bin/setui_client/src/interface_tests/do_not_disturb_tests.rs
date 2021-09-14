// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::do_not_disturb;
use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{DoNotDisturbMarker, DoNotDisturbRequest, DoNotDisturbSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

async fn validate_dnd(
    expected_user_dnd: Option<bool>,
    expected_night_mode_dnd: Option<bool>,
) -> Result<(), Error> {
    let env = create_service!(Services::DoNotDisturb,
        DoNotDisturbRequest::Set { settings, responder } => {
            if let(Some(user_dnd), Some(expected_user_dnd)) =
                (settings.user_initiated_do_not_disturb, expected_user_dnd) {
                assert_eq!(user_dnd, expected_user_dnd);
                responder.send(&mut Ok(()))?;
            } else if let (Some(night_mode_dnd), Some(expected_night_mode_dnd)) =
                (settings.night_mode_initiated_do_not_disturb, expected_night_mode_dnd) {
                assert_eq!(night_mode_dnd, expected_night_mode_dnd);
                responder.send(&mut (Ok(())))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        DoNotDisturbRequest::Watch { responder } => {
            responder.send(DoNotDisturbSettings {
                user_initiated_do_not_disturb: Some(false),
                night_mode_initiated_do_not_disturb: Some(false),
                ..DoNotDisturbSettings::EMPTY
            })?;
        }
    );

    let do_not_disturb_service = env
        .connect_to_protocol::<DoNotDisturbMarker>()
        .context("Failed to connect to do not disturb service")?;

    assert_successful!(do_not_disturb::command(
        do_not_disturb_service,
        expected_user_dnd,
        expected_night_mode_dnd
    ));
    Ok(())
}

#[fuchsia_async::run_until_stalled(test)]
async fn test_do_not_disturb() -> Result<(), Error> {
    println!("do not disturb service tests");
    println!("  client calls dnd watch");
    validate_dnd(Some(false), Some(false)).await?;

    println!("  client calls set user initiated do not disturb");
    validate_dnd(Some(true), Some(false)).await?;

    println!("  client calls set night mode initiated do not disturb");
    validate_dnd(Some(false), Some(true)).await?;

    Ok(())
}
