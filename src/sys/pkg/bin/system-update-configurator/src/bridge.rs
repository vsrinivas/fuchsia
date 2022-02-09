// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Interface and impls to read/write the persistent storage for an [`OptOutPreference`].

use async_trait::async_trait;
use thiserror::Error;

#[cfg(test)]
pub mod testing;

mod vx_ta;

/// A user's preference for which updates to automatically fetch and apply.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OptOutPreference {
    /// Allow all published updates to be automatically fetched and applied.
    AllowAllUpdates = 0,

    /// Allow only critical security updates to be automatically fetched and applied.
    AllowOnlySecurityUpdates,
}

/// An error encountered while accessing the underlying storage for the opt-out setting.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum BridgeError {
    #[error("TEE Internal Error")]
    Internal(#[from] vx_ta::TeeError),
    #[error("TEE Busy")]
    Busy,
}

/// Interface to read/write the persistent storage for an [`OptOutPreference`].
#[async_trait(?Send)]
pub trait Bridge {
    /// Reads the current persisted opt-out preference.
    ///
    /// Defaults to [`OptOutPreference::AllowAllUpdates`] if no preference is set.
    async fn get_opt_out(&self) -> Result<OptOutPreference, BridgeError>;

    /// Sets the persisted opt-out preference to `value`, or returns the error encountered while
    /// attempting to do so.
    async fn set_opt_out(&mut self, value: OptOutPreference) -> Result<(), BridgeError>;
}

/// Implementation of [`Bridge`] that interacts with secure storage on a device.
#[derive(Debug)]
pub struct OptOutStorage;

#[async_trait(?Send)]
impl Bridge for OptOutStorage {
    async fn get_opt_out(&self) -> Result<OptOutPreference, BridgeError> {
        let default_value = OptOutPreference::AllowAllUpdates;
        let value = match vx_ta::ota_config_get(default_value as u32).map_err(map_tee_error)? {
            0 => OptOutPreference::AllowAllUpdates,
            _ => OptOutPreference::AllowOnlySecurityUpdates,
        };
        Ok(value)
    }

    async fn set_opt_out(&mut self, value: OptOutPreference) -> Result<(), BridgeError> {
        vx_ta::ota_config_set(value as u32).map_err(map_tee_error)?;
        Ok(())
    }
}

fn map_tee_error(err: vx_ta::TeeError) -> BridgeError {
    match err {
        vx_ta::TeeError::Busy => BridgeError::Busy,
        vx_ta::TeeError::General(_) => BridgeError::Internal(err),
    }
}
