// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_sme as fidl_sme;
use serde::{Deserialize, Serialize};

/// Enums and structs for wlan client status.
/// These definitions come from fuchsia.wlan.policy/client_provider.fidl
///
#[derive(Serialize, Deserialize, Debug)]
pub enum WlanClientState {
    ConnectionsDisabled = 1,
    ConnectionsEnabled = 2,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ConnectionState {
    Failed = 1,
    Disconnected = 2,
    Connecting = 3,
    Connected = 4,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum SecurityType {
    None = 1,
    Wep = 2,
    Wpa = 3,
    Wpa2 = 4,
    Wpa3 = 5,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum DisconnectStatus {
    TimedOut = 1,
    CredentialsFailed = 2,
    ConnectionStopped = 3,
    ConnectionFailed = 4,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct NetworkIdentifier {
    /// Network name, often used by users to choose between networks in the UI.
    pub ssid: Vec<u8>,
    /// Protection type (or not) for the network.
    pub type_: SecurityType,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct NetworkState {
    /// Network id for the current connection (or attempt).
    pub id: Option<NetworkIdentifier>,
    /// Current state for the connection.
    pub state: Option<ConnectionState>,
    /// Extra information for debugging or Settings display
    pub status: Option<DisconnectStatus>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ClientStateSummary {
    /// State indicating whether wlan will attempt to connect to networks or not.
    pub state: Option<WlanClientState>,

    /// Active connections, connection attempts or failed connections.
    pub networks: Option<Vec<NetworkState>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum Protection {
    Unknown,
    Open,
    Wep,
    Wpa1,
    Wpa1Wpa2Personal,
    Wpa2Personal,
    Wpa2Wpa3Personal,
    Wpa3Personal,
    Wpa2Enterprise,
    Wpa3Enterprise,
}

impl From<fidl_sme::Protection> for Protection {
    fn from(protection: fidl_sme::Protection) -> Self {
        match protection {
            fidl_sme::Protection::Unknown => Protection::Unknown,
            fidl_sme::Protection::Open => Protection::Open,
            fidl_sme::Protection::Wep => Protection::Wep,
            fidl_sme::Protection::Wpa1 => Protection::Wpa1,
            fidl_sme::Protection::Wpa1Wpa2Personal => Protection::Wpa1Wpa2Personal,
            fidl_sme::Protection::Wpa2Personal => Protection::Wpa2Personal,
            fidl_sme::Protection::Wpa2Wpa3Personal => Protection::Wpa2Wpa3Personal,
            fidl_sme::Protection::Wpa3Personal => Protection::Wpa3Personal,
            fidl_sme::Protection::Wpa2Enterprise => Protection::Wpa2Enterprise,
            fidl_sme::Protection::Wpa3Enterprise => Protection::Wpa3Enterprise,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct BssInfo {
    pub bssid: [u8; 6],
    pub ssid: Vec<u8>,
    pub rx_dbm: i8,
    pub snr_db: i8,
    pub channel: u8,
    pub protection: Protection,
    pub compatible: bool,
}

impl From<fidl_sme::BssInfo> for BssInfo {
    fn from(bss_info: fidl_sme::BssInfo) -> Self {
        BssInfo {
            bssid: bss_info.bssid,
            ssid: bss_info.ssid,
            rx_dbm: bss_info.rx_dbm,
            snr_db: bss_info.snr_db,
            channel: bss_info.channel,
            protection: Protection::from(bss_info.protection),
            compatible: bss_info.compatible,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ClientStatusResponse {
    pub connected_to: Option<BssInfo>,
    pub connecting_to_ssid: Vec<u8>,
}

impl From<fidl_sme::ClientStatusResponse> for ClientStatusResponse {
    fn from(status: fidl_sme::ClientStatusResponse) -> Self {
        ClientStatusResponse {
            connected_to: match status.connected_to {
                None => None,
                Some(connected_to) => Some(BssInfo::from(*connected_to)),
            },
            connecting_to_ssid: status.connecting_to_ssid,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum MacRole {
    Client = 1,
    Ap = 2,
    Mesh = 3,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct QueryIfaceResponse {
    pub role: MacRole,
    pub id: u16,
    pub phy_id: u16,
    pub phy_assigned_id: u16,
    pub mac_addr: [u8; 6],
}
