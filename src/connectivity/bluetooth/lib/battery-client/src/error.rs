// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum BatteryClientError {
    /// The `BatteryManager` capability is unavailable.
    #[error("Couldn't connect to `BatteryManager`: {:?}", source)]
    ManagerUnavailable { source: anyhow::Error },
    /// The Battery Watcher stream returned an error.
    #[error("Battery Watcher stream error: {:?}", source)]
    Watcher { source: Box<dyn std::error::Error + Send + Sync> },
    /// There was an error parsing the FIDL Fuchsia Power request.
    #[error("Invalid FIDL Battery Info: {:?}", source)]
    InvalidBatteryInfo { source: Box<dyn std::error::Error + Send + Sync> },
    /// A FIDL error has occurred.
    #[error("FIDL Error occurred: {:?}", source)]
    Fidl { source: fidl::Error },
}

impl BatteryClientError {
    pub fn watcher<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::Watcher { source: e.into() }
    }

    pub fn info<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::InvalidBatteryInfo { source: e.into() }
    }

    pub fn manager_unavailable(e: anyhow::Error) -> Self {
        Self::ManagerUnavailable { source: e }
    }
}

impl From<fidl::Error> for BatteryClientError {
    fn from(source: fidl::Error) -> Self {
        Self::Fidl { source }
    }
}
