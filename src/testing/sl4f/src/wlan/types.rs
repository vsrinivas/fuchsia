// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_sme as fidl_sme,
    serde::{Deserialize, Serialize},
};

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
    Wpa1Wpa2PersonalTkipOnly,
    Wpa2PersonalTkipOnly,
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
            fidl_sme::Protection::Wpa1Wpa2PersonalTkipOnly => Protection::Wpa1Wpa2PersonalTkipOnly,
            fidl_sme::Protection::Wpa2PersonalTkipOnly => Protection::Wpa2PersonalTkipOnly,
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
pub enum BssType {
    Unknown,
    Infrastructure,
    Independent,
    Mesh,
    Personal,
}

impl From<fidl_internal::BssType> for BssType {
    fn from(bss_type: fidl_internal::BssType) -> BssType {
        match bss_type {
            fidl_internal::BssType::Unknown => BssType::Unknown,
            fidl_internal::BssType::Infrastructure => BssType::Infrastructure,
            fidl_internal::BssType::Independent => BssType::Independent,
            fidl_internal::BssType::Mesh => BssType::Mesh,
            fidl_internal::BssType::Personal => BssType::Personal,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ChannelBandwidth {
    Cbw20,
    Cbw40,
    Cbw40Below,
    Cbw80,
    Cbw160,
    Cbw80P80,
}

impl From<fidl_common::ChannelBandwidth> for ChannelBandwidth {
    fn from(cbw: fidl_common::ChannelBandwidth) -> Self {
        match cbw {
            fidl_common::ChannelBandwidth::Cbw20 => ChannelBandwidth::Cbw20,
            fidl_common::ChannelBandwidth::Cbw40 => ChannelBandwidth::Cbw40,
            fidl_common::ChannelBandwidth::Cbw40Below => ChannelBandwidth::Cbw40Below,
            fidl_common::ChannelBandwidth::Cbw80 => ChannelBandwidth::Cbw80,
            fidl_common::ChannelBandwidth::Cbw160 => ChannelBandwidth::Cbw160,
            fidl_common::ChannelBandwidth::Cbw80P80 => ChannelBandwidth::Cbw80P80,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct Channel {
    primary: u8,
    cbw: ChannelBandwidth,
    secondary80: u8,
}

impl From<fidl_common::WlanChannel> for Channel {
    fn from(channel: fidl_common::WlanChannel) -> Self {
        Channel {
            primary: channel.primary,
            cbw: channel.cbw.into(),
            secondary80: channel.secondary80,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct BssDescription {
    bssid: [u8; 6],
    bss_type: BssType,
    beacon_period: u16,
    capability_info: u16,
    ies: Vec<u8>,
    channel: Channel,
    rssi_dbm: i8,
    snr_db: i8,
}

impl From<fidl_internal::BssDescription> for BssDescription {
    fn from(bss_description: fidl_internal::BssDescription) -> Self {
        BssDescription {
            bssid: bss_description.bssid,
            bss_type: bss_description.bss_type.into(),
            beacon_period: bss_description.beacon_period,
            capability_info: bss_description.capability_info,
            ies: bss_description.ies,
            channel: bss_description.channel.into(),
            rssi_dbm: bss_description.rssi_dbm,
            snr_db: bss_description.snr_db,
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ScanResult {
    pub compatible: bool,
    pub timestamp_nanos: i64,
    pub bss_description: BssDescription,
}

impl From<fidl_sme::ScanResult> for ScanResult {
    fn from(scan_result: fidl_sme::ScanResult) -> Self {
        ScanResult {
            compatible: scan_result.compatible,
            timestamp_nanos: scan_result.timestamp_nanos,
            bss_description: scan_result.bss_description.into(),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub struct ServingApInfo {
    pub bssid: [u8; 6],
    pub ssid: Vec<u8>,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    pub channel: u8,
    pub protection: Protection,
}

impl From<fidl_sme::ServingApInfo> for ServingApInfo {
    fn from(client_connection_info: fidl_sme::ServingApInfo) -> Self {
        ServingApInfo {
            bssid: client_connection_info.bssid,
            ssid: client_connection_info.ssid,
            rssi_dbm: client_connection_info.rssi_dbm,
            snr_db: client_connection_info.snr_db,
            channel: client_connection_info.channel.primary,
            protection: Protection::from(client_connection_info.protection),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ClientStatusResponse {
    Connected(ServingApInfo),
    Connecting(Vec<u8>),
    Idle,
}

impl From<fidl_sme::ClientStatusResponse> for ClientStatusResponse {
    fn from(status: fidl_sme::ClientStatusResponse) -> Self {
        match status {
            fidl_sme::ClientStatusResponse::Connected(client_connection_info) => {
                ClientStatusResponse::Connected(client_connection_info.into())
            }
            fidl_sme::ClientStatusResponse::Connecting(ssid) => {
                ClientStatusResponse::Connecting(ssid)
            }
            fidl_sme::ClientStatusResponse::Idle(fidl_sme::Empty {}) => ClientStatusResponse::Idle,
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
    pub sta_addr: [u8; 6],
}
