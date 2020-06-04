// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_location_position::{EmergencyProviderMarker, EmergencyProviderProxy, Position};
use fuchsia_component::client::connect_to_service;

#[derive(Debug)]
pub struct EmergencyProviderFacade {
    provider: EmergencyProviderProxy,
}

impl EmergencyProviderFacade {
    pub fn new() -> Result<EmergencyProviderFacade, Error> {
        Ok(EmergencyProviderFacade { provider: connect_to_service::<EmergencyProviderMarker>()? })
    }

    /// Queries the current `Position`.
    pub async fn get_current(&self) -> Result<Position, Error> {
        self.provider
            .get_current()
            .await
            .context("fidl error")?
            .map_err(|e| format_err!("service error: {:?}", e))
    }
}
