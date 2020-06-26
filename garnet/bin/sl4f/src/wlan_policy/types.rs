// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_wlan_policy as fidl_policy, serde::Serialize};

/// Structs for wlan policy to go through SL4F
#[derive(Serialize)]
pub enum WlanClientState {
    ConnectionsDisabled,
    ConnectionsEnabled,
}

impl From<fidl_policy::WlanClientState> for WlanClientState {
    fn from(state: fidl_policy::WlanClientState) -> Self {
        match state {
            fidl_policy::WlanClientState::ConnectionsDisabled => Self::ConnectionsDisabled,
            fidl_policy::WlanClientState::ConnectionsEnabled => Self::ConnectionsEnabled,
        }
    }
}

#[derive(Serialize)]
pub enum ConnectionState {
    Failed,
    Disconnected,
    Connecting,
    Connected,
}

impl From<fidl_policy::ConnectionState> for ConnectionState {
    fn from(state: fidl_policy::ConnectionState) -> Self {
        match state {
            fidl_policy::ConnectionState::Failed => Self::Failed,
            fidl_policy::ConnectionState::Disconnected => Self::Disconnected,
            fidl_policy::ConnectionState::Connecting => Self::Connecting,
            fidl_policy::ConnectionState::Connected => Self::Connected,
        }
    }
}

#[derive(Serialize)]
pub enum SecurityType {
    None = 1,
    Wep = 2,
    Wpa = 3,
    Wpa2 = 4,
    Wpa3 = 5,
}

#[derive(Serialize)]
pub enum DisconnectStatus {
    TimedOut = 1,
    CredentialsFailed = 2,
    ConnectionStopped = 3,
    ConnectionFailed = 4,
}

impl From<fidl_policy::DisconnectStatus> for DisconnectStatus {
    fn from(status: fidl_policy::DisconnectStatus) -> Self {
        match status {
            fidl_policy::DisconnectStatus::TimedOut => Self::TimedOut,
            fidl_policy::DisconnectStatus::CredentialsFailed => Self::CredentialsFailed,
            fidl_policy::DisconnectStatus::ConnectionStopped => Self::ConnectionStopped,
            fidl_policy::DisconnectStatus::ConnectionFailed => Self::ConnectionFailed,
        }
    }
}

#[derive(Serialize)]
pub struct NetworkIdentifier {
    /// Network name, often used by users to choose between networks in the UI.
    pub ssid: String,
    /// Protection type (or not) for the network.
    pub type_: SecurityType,
}

#[derive(Serialize)]
pub struct NetworkState {
    /// Network id for the current connection (or attempt).
    pub id: Option<NetworkIdentifier>,
    /// Current state for the connection.
    pub state: Option<ConnectionState>,
    /// Extra information for debugging or Settings display
    pub status: Option<DisconnectStatus>,
}

impl From<fidl_policy::NetworkState> for NetworkState {
    fn from(state: fidl_policy::NetworkState) -> Self {
        NetworkState {
            id: state.id.map(NetworkIdentifier::from),
            state: state.state.map(ConnectionState::from),
            status: state.status.map(DisconnectStatus::from),
        }
    }
}

#[derive(Serialize)]
pub struct ClientStateSummary {
    /// State indicating whether wlan will attempt to connect to networks or not.
    pub state: Option<WlanClientState>,

    /// Active connections, connection attempts or failed connections.
    pub networks: Option<Vec<NetworkState>>,
}

impl From<fidl_policy::ClientStateSummary> for ClientStateSummary {
    fn from(summary: fidl_policy::ClientStateSummary) -> ClientStateSummary {
        ClientStateSummary {
            state: summary.state.map(WlanClientState::from),
            networks: summary
                .networks
                .map(|networks| networks.into_iter().map(|state| state.into()).collect()),
        }
    }
}

/// Serializable credential value for SL4F. The unkown variant should not be used - if an unkown
/// variant shows up somewhere, there may be an issue with the conversion from FIDL value to this.
#[derive(Serialize)]
pub enum Credential {
    Password(String),
    Psk(String),
    None,
    Unknown,
}

impl From<fidl_policy::Credential> for Credential {
    fn from(credential: fidl_policy::Credential) -> Self {
        match credential {
            fidl_policy::Credential::Password(password) => {
                Self::Password(String::from_utf8_lossy(&password).to_string())
            }
            fidl_policy::Credential::Psk(psk) => Self::Psk(hex::encode(psk)),
            fidl_policy::Credential::None(_) => Self::None,
            _ => Self::Unknown,
        }
    }
}

impl Credential {
    fn get_type(&self) -> String {
        match self {
            Credential::Password(_) => "Password",
            Credential::Psk(_) => "Psk",
            Credential::None => "None",
            Credential::Unknown => "Unknown",
        }
        .to_string()
    }

    fn into_value(self) -> String {
        match self {
            Credential::Password(password) => password,
            Credential::Psk(psk) => psk,
            _ => String::new(),
        }
    }
}

impl From<fidl_policy::NetworkIdentifier> for NetworkIdentifier {
    fn from(id: fidl_policy::NetworkIdentifier) -> Self {
        Self { ssid: String::from_utf8_lossy(&id.ssid).to_string(), type_: id.type_.into() }
    }
}

impl From<fidl_policy::SecurityType> for SecurityType {
    fn from(security: fidl_policy::SecurityType) -> Self {
        match security {
            fidl_policy::SecurityType::None => SecurityType::None,
            fidl_policy::SecurityType::Wep => SecurityType::Wep,
            fidl_policy::SecurityType::Wpa => SecurityType::Wpa,
            fidl_policy::SecurityType::Wpa2 => SecurityType::Wpa2,
            fidl_policy::SecurityType::Wpa3 => SecurityType::Wpa3,
        }
    }
}

/// A NetworkConfig that is SL4F friendly and easier to use in tests. Byte vector fields are
/// Strings instead so that tests can check the values more simply and PSKs are translated
/// into hex.
#[derive(Serialize)]
pub struct NetworkConfig {
    pub ssid: Option<String>,
    pub security_type: Option<SecurityType>,
    pub credential_type: Option<String>,
    pub credential_value: Option<String>,
}

impl From<fidl_policy::NetworkConfig> for NetworkConfig {
    fn from(cfg: fidl_policy::NetworkConfig) -> Self {
        let credential = cfg.credential.map(Credential::from);
        NetworkConfig {
            ssid: cfg.id.as_ref().map(|id| String::from_utf8_lossy(&id.ssid).to_string()),
            security_type: cfg.id.map(|id| SecurityType::from(id.type_)),
            credential_type: credential.as_ref().map(|credential| credential.get_type()),
            credential_value: credential.map(|credential| credential.into_value()),
        }
    }
}
