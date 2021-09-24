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

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_sme::Protection")]
pub(crate) enum ProtectionDef {
    Unknown = 0,
    Open = 1,
    Wep = 2,
    Wpa1 = 3,
    Wpa1Wpa2PersonalTkipOnly = 4,
    Wpa2PersonalTkipOnly = 5,
    Wpa1Wpa2Personal = 6,
    Wpa2Personal = 7,
    Wpa2Wpa3Personal = 8,
    Wpa3Personal = 9,
    Wpa2Enterprise = 10,
    Wpa3Enterprise = 11,
}

// The following definitions derive Serialize and Deserialize for remote types, i.e. types
// defined in other crates. See https://serde.rs/remote-derive.html for more info.
#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_common::ChannelBandwidth")]
#[repr(u32)]
pub(crate) enum ChannelBandwidthDef {
    Cbw20 = 0,
    Cbw40 = 1,
    Cbw40Below = 2,
    Cbw80 = 3,
    Cbw160 = 4,
    Cbw80P80 = 5,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_common::WlanChannel")]
pub(crate) struct WlanChannelDef {
    pub primary: u8,
    #[serde(with = "ChannelBandwidthDef")]
    pub cbw: fidl_common::ChannelBandwidth,
    pub secondary80: u8,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_internal::BssType")]
pub(crate) enum BssTypeDef {
    Infrastructure = 1,
    Personal = 2,
    Independent = 3,
    Mesh = 4,
    Unknown = 255,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "fidl_internal::BssDescription")]
pub(crate) struct BssDescriptionDef {
    pub bssid: [u8; 6],
    #[serde(with = "BssTypeDef")]
    pub bss_type: fidl_internal::BssType,
    pub beacon_period: u16,
    pub capability_info: u16,
    pub ies: Vec<u8>,
    #[serde(with = "WlanChannelDef")]
    pub channel: fidl_common::WlanChannel,
    pub rssi_dbm: i8,
    pub snr_db: i8,
}

#[derive(Serialize)]
pub(crate) struct BssDescriptionWrapper(
    #[serde(with = "BssDescriptionDef")] pub fidl_internal::BssDescription,
);

#[derive(Serialize)]
#[serde(remote = "fidl_sme::ServingApInfo")]
pub(crate) struct ServingApInfoDef {
    pub bssid: [u8; 6],
    pub ssid: Vec<u8>,
    pub rssi_dbm: i8,
    pub snr_db: i8,
    #[serde(with = "WlanChannelDef")]
    pub channel: fidl_common::WlanChannel,
    #[serde(with = "ProtectionDef")]
    pub protection: fidl_sme::Protection,
}

#[derive(Serialize)]
#[serde(remote = "fidl_sme::Empty")]
pub(crate) struct SmeEmptyDef;

#[derive(Serialize)]
#[serde(remote = "fidl_sme::ClientStatusResponse")]
pub(crate) enum ClientStatusResponseDef {
    Connected(#[serde(with = "ServingApInfoDef")] fidl_sme::ServingApInfo),
    Connecting(Vec<u8>),
    #[serde(with = "SmeEmptyDef")]
    Idle(fidl_sme::Empty),
}

#[derive(Serialize)]
pub(crate) struct ClientStatusResponseWrapper(
    #[serde(with = "ClientStatusResponseDef")] pub fidl_sme::ClientStatusResponse,
);

#[derive(Serialize)]
#[serde(remote = "fidl_fuchsia_wlan_device::MacRole")]
pub(crate) enum MacRoleDef {
    Client = 1,
    Ap = 2,
    Mesh = 3,
}

#[derive(Serialize)]
#[serde(remote = "fidl_common::DriverFeature")]
pub(crate) enum DriverFeatureDef {
    ScanOffload = 0,
    RateSelection = 1,
    Synth = 2,
    TxStatusReport = 3,
    Dfs = 4,
    ProbeRespOffload = 5,
    SaeSmeAuth = 6,
    SaeDriverAuth = 7,
    Mfp = 8,
    TempSoftmac = 2718281828,
}
#[derive(Serialize)]
pub(crate) struct DriverFeatureWrapper(
    #[serde(with = "DriverFeatureDef")] pub fidl_common::DriverFeature,
);

#[derive(Serialize)]
pub(crate) struct QueryIfaceResponseDef {
    #[serde(with = "MacRoleDef")]
    pub role: fidl_fuchsia_wlan_device::MacRole,
    pub id: u16,
    pub phy_id: u16,
    pub phy_assigned_id: u16,
    pub sta_addr: [u8; 6],
    pub driver_features: Vec<DriverFeatureWrapper>,
}

#[derive(Serialize)]
pub(crate) struct QueryIfaceResponseWrapper(pub QueryIfaceResponseDef);

impl From<fidl_fuchsia_wlan_device_service::QueryIfaceResponse> for QueryIfaceResponseDef {
    fn from(resp: fidl_fuchsia_wlan_device_service::QueryIfaceResponse) -> QueryIfaceResponseDef {
        QueryIfaceResponseDef {
            role: resp.role,
            id: resp.id,
            phy_id: resp.phy_id,
            phy_assigned_id: resp.phy_assigned_id,
            sta_addr: resp.sta_addr,
            driver_features: resp
                .driver_features
                .iter()
                .map(|df| DriverFeatureWrapper(*df))
                .collect(),
        }
    }
}
