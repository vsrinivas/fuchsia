// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_policy as fidl_policy;

pub type NetworkIdentifier = fidl_policy::NetworkIdentifier;
pub type SecurityType = fidl_policy::SecurityType;
pub type ConnectionState = fidl_policy::ConnectionState;
pub type DisconnectStatus = fidl_policy::DisconnectStatus;
pub type Compatibility = fidl_policy::Compatibility;
pub type WlanChan = fidl_common::WlanChan;

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
            ..fidl_policy::ScanResult::empty()
        }
    }
}

// An internal version of fidl_policy::Bss with extended information
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct Bss {
    /// MAC address for the AP interface.
    pub bssid: [u8; 6],
    /// Calculated received signal strength for the beacon/probe response.
    pub rssi: i8,
    /// Signal to noise ratio  for the beacon/probe response.
    pub snr_db: i8,
    /// Operating frequency for this network (in MHz).
    pub frequency: u32,
    /// Channel for this network.
    pub channel: WlanChan,
    /// Realtime timestamp for this scan result entry.
    pub timestamp_nanos: i64,
    /// Seen in a passive scan.
    pub observed_in_passive_scan: bool,
    /// Compatible with this device's network stack.
    pub compatible: bool,
}
impl From<Bss> for fidl_policy::Bss {
    fn from(input: Bss) -> Self {
        fidl_policy::Bss {
            bssid: Some(input.bssid),
            rssi: Some(input.rssi),
            frequency: Some(input.frequency),
            timestamp_nanos: Some(input.timestamp_nanos),
            ..fidl_policy::Bss::empty()
        }
    }
}

/// Additional data used during network selection and connection.
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct NetworkSelectionMetadata {
    pub observed_in_passive_scan: bool,
}
