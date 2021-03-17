// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config_management,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    wlan_common::channel::Channel,
    wlan_metrics_registry::{
        PolicyConnectionAttemptMetricDimensionReason, PolicyDisconnectionMetricDimensionReason,
    },
};

#[cfg(test)]
pub(crate) use crate::regulatory_manager::REGION_CODE_LEN;

pub type NetworkIdentifier = fidl_policy::NetworkIdentifier;
pub type SecurityType = fidl_policy::SecurityType;
pub type ConnectionState = fidl_policy::ConnectionState;
pub type DisconnectStatus = fidl_policy::DisconnectStatus;
pub type Compatibility = fidl_policy::Compatibility;
pub type WlanChan = fidl_common::WlanChan;
pub type Bssid = [u8; 6];
pub type DisconnectReason = PolicyDisconnectionMetricDimensionReason;
pub type ConnectReason = PolicyConnectionAttemptMetricDimensionReason;

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
    pub id: NetworkIdentifier,
    /// Individual access points offering the specified network.
    pub entries: Vec<Bss>,
    /// Indication if the detected network is supported by the implementation.
    pub compatibility: Compatibility,
}
impl From<ScanResult> for fidl_policy::ScanResult {
    fn from(input: ScanResult) -> Self {
        fidl_policy::ScanResult {
            id: Some(input.id),
            entries: Some(input.entries.into_iter().map(fidl_policy::Bss::from).collect()),
            compatibility: Some(input.compatibility),
            ..fidl_policy::ScanResult::EMPTY
        }
    }
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
    pub bss_desc: Option<Box<fidl_internal::BssDescription>>,
}
impl From<Bss> for fidl_policy::Bss {
    fn from(input: Bss) -> Self {
        // Get the frequency. On error, default to Some(0) rather than None to protect against
        // consumer code that expects this field to always be set.
        let frequency = Channel::from_fidl(input.channel).get_center_freq().unwrap_or(0);
        fidl_policy::Bss {
            bssid: Some(input.bssid),
            rssi: Some(input.rssi),
            frequency: Some(frequency.into()), // u16.into() -> u32
            timestamp_nanos: Some(input.timestamp_nanos),
            ..fidl_policy::Bss::EMPTY
        }
    }
}

/// Data for connecting to a specific network and keeping track of what is connected to.
#[derive(Clone, Debug, PartialEq)]
pub struct ConnectionCandidate {
    pub network: NetworkIdentifier,
    pub credential: config_management::Credential,
    pub bss: Option<Box<fidl_internal::BssDescription>>,
    /// Temporarily an Option<>, since this information comes from a scan, and scans are not always
    /// performed in the Policy layer right now. TODO(53899) Remove the optionality once all scans
    /// are done at the Policy layer.
    pub observed_in_passive_scan: Option<bool>,
    pub multiple_bss_candidates: Option<bool>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConnectRequest {
    pub target: ConnectionCandidate,
    pub reason: ConnectReason,
}
