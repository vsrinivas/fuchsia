// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_policy as fidl_policy;

pub type NetworkIdentifier = crate::config_management::network_config::NetworkIdentifier;
pub type SecurityType = crate::config_management::network_config::SecurityType;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OperatingState {
    Failed,
    Starting,
    Active,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ConnectivityMode {
    LocalOnly,
    Unrestricted,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OperatingBand {
    Any,
    Only24Ghz,
    Only5Ghz,
}

impl From<OperatingState> for fidl_policy::OperatingState {
    fn from(state: OperatingState) -> Self {
        match state {
            OperatingState::Failed => fidl_policy::OperatingState::Failed,
            OperatingState::Starting => fidl_policy::OperatingState::Starting,
            OperatingState::Active => fidl_policy::OperatingState::Active,
        }
    }
}

impl From<ConnectivityMode> for fidl_policy::ConnectivityMode {
    fn from(mode: ConnectivityMode) -> Self {
        match mode {
            ConnectivityMode::LocalOnly => fidl_policy::ConnectivityMode::LocalOnly,
            ConnectivityMode::Unrestricted => fidl_policy::ConnectivityMode::Unrestricted,
        }
    }
}

impl From<OperatingBand> for fidl_policy::OperatingBand {
    fn from(band: OperatingBand) -> Self {
        match band {
            OperatingBand::Any => fidl_policy::OperatingBand::Any,
            OperatingBand::Only24Ghz => fidl_policy::OperatingBand::Only24Ghz,
            OperatingBand::Only5Ghz => fidl_policy::OperatingBand::Only5Ghz,
        }
    }
}

impl From<fidl_policy::OperatingState> for OperatingState {
    fn from(state: fidl_policy::OperatingState) -> Self {
        match state {
            fidl_policy::OperatingState::Failed => OperatingState::Failed,
            fidl_policy::OperatingState::Starting => OperatingState::Starting,
            fidl_policy::OperatingState::Active => OperatingState::Active,
        }
    }
}

impl From<fidl_policy::ConnectivityMode> for ConnectivityMode {
    fn from(mode: fidl_policy::ConnectivityMode) -> Self {
        match mode {
            fidl_policy::ConnectivityMode::LocalOnly => ConnectivityMode::LocalOnly,
            fidl_policy::ConnectivityMode::Unrestricted => ConnectivityMode::Unrestricted,
        }
    }
}

impl From<fidl_policy::OperatingBand> for OperatingBand {
    fn from(band: fidl_policy::OperatingBand) -> Self {
        match band {
            fidl_policy::OperatingBand::Any => OperatingBand::Any,
            fidl_policy::OperatingBand::Only24Ghz => OperatingBand::Only24Ghz,
            fidl_policy::OperatingBand::Only5Ghz => OperatingBand::Only5Ghz,
        }
    }
}
