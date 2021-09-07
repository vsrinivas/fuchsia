// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Services;
use crate::ENV_NAME;
use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{FactoryResetMarker, FactoryResetRequest, FactoryResetSettings};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;
use setui_client_lib::factory_reset;

// Validates the set and watch for factory reset.
pub(crate) async fn validate_factory_reset(
    expected_local_reset_allowed: bool,
) -> Result<(), Error> {
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
