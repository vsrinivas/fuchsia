// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config_management,
    fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme, fuchsia_zircon as zx,
    std::collections::HashSet,
    wlan_common::{self, security::SecurityDescriptor},
    wlan_metrics_registry::{
        PolicyConnectionAttemptMigratedMetricDimensionReason,
        PolicyDisconnectionMigratedMetricDimensionReason,
    },
};

#[cfg(test)]
pub(crate) use crate::regulatory_manager::REGION_CODE_LEN;

pub type NetworkIdentifier = config_management::network_config::NetworkIdentifier;
pub type SecurityTypeDetailed = fidl_sme::Protection;
pub type SecurityType = config_management::network_config::SecurityType;
pub type ConnectionState = fidl_policy::ConnectionState;
pub type DisconnectStatus = fidl_policy::DisconnectStatus;
pub type Compatibility = fidl_policy::Compatibility;
pub type WlanChan = wlan_common::channel::Channel;
pub type Cbw = wlan_common::channel::Cbw;
pub use ieee80211::Bssid;
pub use ieee80211::Ssid;
pub type DisconnectReason = PolicyDisconnectionMigratedMetricDimensionReason;
pub type ConnectReason = PolicyConnectionAttemptMigratedMetricDimensionReason;
pub type ScanError = fidl_policy::ScanErrorCode;

pub fn convert_to_sme_disconnect_reason(
    disconnect_reason: PolicyDisconnectionMigratedMetricDimensionReason,
) -> fidl_sme::UserDisconnectReason {
    match disconnect_reason {
        PolicyDisconnectionMigratedMetricDimensionReason::Unknown => {
            fidl_sme::UserDisconnectReason::Unknown
        }
        PolicyDisconnectionMigratedMetricDimensionReason::FailedToConnect => {
            fidl_sme::UserDisconnectReason::FailedToConnect
        }
        PolicyDisconnectionMigratedMetricDimensionReason::FidlConnectRequest => {
            fidl_sme::UserDisconnectReason::FidlConnectRequest
        }
        PolicyDisconnectionMigratedMetricDimensionReason::FidlStopClientConnectionsRequest => {
            fidl_sme::UserDisconnectReason::FidlStopClientConnectionsRequest
        }
        PolicyDisconnectionMigratedMetricDimensionReason::ProactiveNetworkSwitch => {
            fidl_sme::UserDisconnectReason::ProactiveNetworkSwitch
        }
        PolicyDisconnectionMigratedMetricDimensionReason::DisconnectDetectedFromSme => {
            fidl_sme::UserDisconnectReason::DisconnectDetectedFromSme
        }
        PolicyDisconnectionMigratedMetricDimensionReason::RegulatoryRegionChange => {
            fidl_sme::UserDisconnectReason::RegulatoryRegionChange
        }
        PolicyDisconnectionMigratedMetricDimensionReason::Startup => {
            fidl_sme::UserDisconnectReason::Startup
        }
        PolicyDisconnectionMigratedMetricDimensionReason::NetworkUnsaved => {
            fidl_sme::UserDisconnectReason::NetworkUnsaved
        }
        PolicyDisconnectionMigratedMetricDimensionReason::NetworkConfigUpdated => {
            fidl_sme::UserDisconnectReason::NetworkConfigUpdated
        }
    }
}

// An internal version of fidl_policy::ScanResult that can be cloned
#[derive(Debug, Clone, PartialEq)]
pub struct ScanResult {
    /// Network properties used to distinguish between networks and to group
    /// individual APs.
    pub ssid: Ssid,
    pub security_type_detailed: SecurityTypeDetailed,
    /// Individual access points offering the specified network.
    pub entries: Vec<Bss>,
    /// Indication if the detected network is supported by the implementation.
    pub compatibility: Compatibility,
}

pub struct NetworkIdentifierDetailed {
    pub ssid: Ssid,
    pub security_type: SecurityTypeDetailed,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ScanObservation {
    Passive,
    Active,
    Unknown,
}

// An internal version of fidl_policy::Bss with extended information
#[derive(Debug, Clone, PartialEq)]
pub struct Bss {
    /// MAC address for the AP interface.
    pub bssid: Bssid,
    /// Calculated received signal strength for the beacon/probe response.
    pub rssi: i8,
    /// Signal to noise ratio  for the beacon/probe response.
    pub snr_db: i8,
    /// Channel for this network.
    pub channel: WlanChan,
    /// Realtime timestamp for this scan result entry.
    pub timestamp: zx::Time,
    /// The scanning mode used to observe the BSS.
    pub observation: ScanObservation,
    /// Compatibility with this device's network stack.
    pub compatibility: Option<wlan_common::scan::Compatibility>,
    /// The BSS description with information that SME needs for connecting.
    pub bss_description: fidl_internal::BssDescription,
}

impl Bss {
    pub fn is_compatible(&self) -> bool {
        self.compatibility.is_some()
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct ScannedCandidate {
    pub bss_description: fidl_internal::BssDescription,
    pub observation: ScanObservation,
    pub has_multiple_bss_candidates: bool,
    pub mutual_security_protocols: HashSet<SecurityDescriptor>,
}

/// Data for connecting to a specific network and keeping track of what is connected to.
#[derive(Clone, Debug, PartialEq)]
pub struct ConnectionCandidate {
    pub network: NetworkIdentifier,
    pub credential: config_management::Credential,
    pub scanned: Option<ScannedCandidate>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConnectRequest {
    pub target: ConnectionCandidate,
    pub reason: ConnectReason,
}
