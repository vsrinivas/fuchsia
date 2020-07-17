// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Serialize;

/// Supported Weave commands.
pub enum WeaveMethod {
    GetPairingCode,
    GetQrCode,
    GetPairingState,
}

impl std::str::FromStr for WeaveMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetPairingCode" => Ok(WeaveMethod::GetPairingCode),
            "GetPairingState" => Ok(WeaveMethod::GetPairingState),
            "GetQrCode" => Ok(WeaveMethod::GetQrCode),
            _ => return Err(format_err!("invalid Weave FIDL method: {}", method)),
        }
    }
}

#[derive(Serialize, PartialEq, Copy, Clone, Debug)]
pub struct PairingState {
    /// Has Weave been fully provisioned? This implies that all provisioning
    /// has been completed as expected as specified in the configuration.
    pub is_weave_fully_provisioned: Option<bool>,
    /// Has WiFi been provisioned? Defaults to false.
    pub is_wlan_provisioned: Option<bool>,
    /// Has Thread been provisioned? Defaults to false.
    pub is_thread_provisioned: Option<bool>,
    /// Has the fabric been provisioned? Defaults to false.
    pub is_fabric_provisioned: Option<bool>,
    /// Has the service been provisioned? Defaults to false.
    pub is_service_provisioned: Option<bool>,
}

impl From<PairingState> for fidl_fuchsia_weave::PairingState {
    fn from(item: PairingState) -> Self {
        fidl_fuchsia_weave::PairingState {
            is_weave_fully_provisioned: item.is_weave_fully_provisioned,
            is_wlan_provisioned: item.is_wlan_provisioned,
            is_thread_provisioned: item.is_thread_provisioned,
            is_fabric_provisioned: item.is_fabric_provisioned,
            is_service_provisioned: item.is_service_provisioned,
        }
    }
}

impl From<fidl_fuchsia_weave::PairingState> for PairingState {
    fn from(item: fidl_fuchsia_weave::PairingState) -> Self {
        PairingState {
            is_weave_fully_provisioned: item.is_weave_fully_provisioned,
            is_wlan_provisioned: item.is_wlan_provisioned,
            is_thread_provisioned: item.is_thread_provisioned,
            is_fabric_provisioned: item.is_fabric_provisioned,
            is_service_provisioned: item.is_service_provisioned,
        }
    }
}
