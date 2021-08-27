// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config_management,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    wlan_metrics_registry::{
        PolicyConnectionAttemptMetricDimensionReason, PolicyDisconnectionMetricDimensionReason,
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
pub type WlanChan = fidl_common::WlanChannel;
pub use ieee80211::Bssid;
pub use ieee80211::Ssid;
pub type DisconnectReason = PolicyDisconnectionMetricDimensionReason;
pub type ConnectReason = PolicyConnectionAttemptMetricDimensionReason;
pub type ScanError = fidl_policy::ScanErrorCode;

pub fn convert_to_sme_disconnect_reason(
    disconnect_reason: PolicyDisconnectionMetricDimensionReason,
) -> fidl_sme::UserDisconnectReason {
    match disconnect_reason {
        PolicyDisconnectionMetricDimensionReason::Unknown => {
            fidl_sme::UserDisconnectReason::Unknown
        }
        PolicyDisconnectionMetricDimensionReason::FailedToConnect => {
            fidl_sme::UserDisconnectReason::FailedToConnect
        }
        PolicyDisconnectionMetricDimensionReason::FidlConnectRequest => {
            fidl_sme::UserDisconnectReason::FidlConnectRequest
        }
        PolicyDisconnectionMetricDimensionReason::FidlStopClientConnectionsRequest => {
            fidl_sme::UserDisconnectReason::FidlStopClientConnectionsRequest
        }
        PolicyDisconnectionMetricDimensionReason::ProactiveNetworkSwitch => {
            fidl_sme::UserDisconnectReason::ProactiveNetworkSwitch
        }
        PolicyDisconnectionMetricDimensionReason::DisconnectDetectedFromSme => {
            fidl_sme::UserDisconnectReason::DisconnectDetectedFromSme
        }
        PolicyDisconnectionMetricDimensionReason::RegulatoryRegionChange => {
            fidl_sme::UserDisconnectReason::RegulatoryRegionChange
        }
        PolicyDisconnectionMetricDimensionReason::Startup => {
            fidl_sme::UserDisconnectReason::Startup
        }
        PolicyDisconnectionMetricDimensionReason::NetworkUnsaved => {
            fidl_sme::UserDisconnectReason::NetworkUnsaved
        }
        PolicyDisconnectionMetricDimensionReason::NetworkConfigUpdated => {
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
    pub timestamp_nanos: i64,
    /// Seen in a passive scan.
    pub observed_in_passive_scan: bool,
    /// Compatible with this device's network stack.
    pub compatible: bool,
    /// The BSS description with information that SME needs for connecting.
    pub bss_description: fidl_internal::BssDescription,
}

/// Data for connecting to a specific network and keeping track of what is connected to.
#[derive(Clone, Debug, PartialEq)]
pub struct ConnectionCandidate {
    pub network: NetworkIdentifier,
    pub credential: config_management::Credential,
    // TODO(fxbug.dev/72906): Move these optional fields into a `struct` that makes it clear that
    //                        this information is related and derived from latent scans. In
    //                        `ConnectionCandidate`, replace these fields with a single optional
    //                        field of the new `struct`.
    pub bss_description: Option<fidl_internal::BssDescription>,
    pub observed_in_passive_scan: Option<bool>,
    pub multiple_bss_candidates: Option<bool>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConnectRequest {
    pub target: ConnectionCandidate,
    pub reason: ConnectReason,
}
