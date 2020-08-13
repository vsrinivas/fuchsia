// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl_fuchsia_time::{UtcMarker, UtcSource};
use fuchsia_component::client::connect_to_service;
use std::convert::TryInto;
use std::time::SystemTime;

/// Facade providing access to system time.
#[derive(Debug)]
pub struct TimeFacade {}

impl TimeFacade {
    pub fn new() -> Self {
        TimeFacade {}
    }

    /// Returns the system's reported UTC time in millis since the Unix epoch.
    pub fn system_time_millis() -> Result<u64, Error> {
        let time_millis = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH)?.as_millis();
        // Zircon stores time as 64 bits so truncating from u128 should not fail.
        Ok(time_millis.try_into()?)
    }

    /// Returns true iff system time has been synchronized with some source.
    pub async fn is_synchronized() -> Result<bool, Error> {
        // Since we establish a new connection every time, watch should return immediately.
        let utc_proxy = connect_to_service::<UtcMarker>()?;
        let state = utc_proxy.watch_state().await?;
        let source = state.source.ok_or(anyhow!("Utc service returned no source"))?;
        Ok(match source {
            UtcSource::Backstop => false,
            UtcSource::External => true,
        })
    }
}
