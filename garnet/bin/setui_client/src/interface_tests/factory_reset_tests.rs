// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::factory_reset;
use crate::interface_tests::Services;
use crate::interface_tests::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{FactoryResetMarker, FactoryResetRequest, FactoryResetSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

// Validates the set and watch for factory reset.
#[fuchsia_async::run_until_stalled(test)]
async fn validate_factory_reset() -> Result<(), Error> {
    // Test client calls set local reset allowed.
    let expected_local_reset_allowed = true;
    let env = create_service!(
        Services::FactoryReset, FactoryResetRequest::Set { settings, responder, } => {
            if let (Some(local_reset_allowed), expected_local_reset_allowed) =
                (settings.is_local_reset_allowed, expected_local_reset_allowed)
            {
                assert_eq!(local_reset_allowed, expected_local_reset_allowed);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        FactoryResetRequest::Watch { responder } => {
            responder.send(FactoryResetSettings {
                is_local_reset_allowed: Some(true),
                ..FactoryResetSettings::EMPTY
            })?;
        }
    );

    let factory_reset_service = env
        .connect_to_protocol::<FactoryResetMarker>()
        .context("Failed to connect to factory reset service")?;

    assert_successful!(factory_reset::command(
        factory_reset_service,
        Some(expected_local_reset_allowed)
    ));

    Ok(())
}
