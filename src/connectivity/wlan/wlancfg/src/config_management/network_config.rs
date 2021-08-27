// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::types as client_types,
    arbitrary::Arbitrary,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_policy as fidl_policy,
    fuchsia_zircon as zx,
    serde::{Deserialize, Serialize},
    std::{
        collections::VecDeque,
        convert::TryFrom,
        fmt::{self, Debug},
    },
};

/// The maximum number of denied connection reasons we will store for one network at a time.
/// For now this number is chosen arbitrarily.
const NUM_DENY_REASONS: usize = 20;
/// The max number of quick disconnects we will store for one network at a time. For now this
/// number is chosen arbitrarily.
const NUM_DISCONNECTS: usize = 20;
/// constants for the constraints on valid credential values
const WEP_40_ASCII_LEN: usize = 5;
const WEP_40_HEX_LEN: usize = 10;
const WEP_104_ASCII_LEN: usize = 13;
const WEP_104_HEX_LEN: usize = 26;
const WPA_MIN_PASSWORD_LEN: usize = 8;
const WPA_MAX_PASSWORD_LEN: usize = 63;
pub const WPA_PSK_BYTE_LEN: usize = 32;
/// If we have seen a network in a passive scan, we will rarely actively scan for it.
pub const PROB_HIDDEN_IF_SEEN_PASSIVE: f32 = 0.05;
/// If we have connected to a network from a passive scan, we will never scan for it.
pub const PROB_HIDDEN_IF_CONNECT_PASSIVE: f32 = 0.0;
/// If we connected to a network after we had to scan actively to find it, it is likely hidden.
pub const PROB_HIDDEN_IF_CONNECT_ACTIVE: f32 = 0.95;
/// Default probability that we will actively scan for the network if we haven't seen it in any
/// passive scan.
pub const PROB_HIDDEN_DEFAULT: f32 = 0.9;
/// The lowest we will set the probability for actively scanning for a network.
pub const PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE: f32 = 0.25;
/// How much we will lower the probability of scanning for an active network if we don't see the
/// network in an active scan.
pub const PROB_HIDDEN_INCREMENT_NOT_SEEN_ACTIVE: f32 = 0.14;

pub type SaveError = fidl_policy::NetworkConfigChangeError;

/// In-memory history of things that we need to know to calculated hidden network probability.
#[derive(Clone, Debug, PartialEq)]
struct HiddenProbabilityStats {
    pub connected_active: bool,
}

impl HiddenProbabilityStats {
    fn new() -> Self {
        HiddenProbabilityStats { connected_active: false }
    }
}

/// History of connects, disconnects, and connection strength to estimate whether we can establish
/// and maintain connection with a network and if it is weakening. Used in choosing best network.
#[derive(Clone, Debug, PartialEq)]
pub struct PerformanceStats {
    /// List of recent connection failures, used to determine whether we should try connecting
    /// to a network again. Capacity of list is at least NUM_DENY_REASONS.
    pub failure_list: ConnectFailureList,
    /// List of recent disconnects where the connect duration was short.
    pub disconnect_list: DisconnectList,
}

impl PerformanceStats {
    pub fn new() -> Self {
        Self { failure_list: ConnectFailureList::new(), disconnect_list: DisconnectList::new() }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum FailureReason {
    // Failed to join because the authenticator did not accept the credentials provided.
    CredentialRejected,
    // Failed to join for other reason, mapped from SME ConnectResultCode::Failed
    GeneralFailure,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ConnectFailure {
    /// For determining whether this connection failure is still relevant
    pub time: zx::Time,
    /// The reason that connection failed
    pub reason: FailureReason,
    /// The BSSID that we failed to connect to
    pub bssid: client_types::Bssid,
}

/// Ring buffer that holds network denials. It starts empty and replaces oldest when full.
#[derive(Clone, Debug, PartialEq)]
pub struct ConnectFailureList(VecDeque<ConnectFailure>);

impl ConnectFailureList {
    /// The max stored number of deny reasons is at least NUM_DENY_REASONS, decided by VecDeque
    pub fn new() -> Self {
        Self(VecDeque::with_capacity(NUM_DENY_REASONS))
    }

    /// Record network denial information in the network config, dropping the oldest information
    /// if the list of denial reasons is already full before adding.
    pub fn add(&mut self, bssid: client_types::Bssid, reason: FailureReason) {
        if self.0.len() == self.0.capacity() {
            let _ = self.0.pop_front();
        }
        self.0.push_back(ConnectFailure { time: zx::Time::get_monotonic(), reason, bssid });
    }

    /// Returns a list of the denials that happened at or after the given monotonic time.
    pub fn get_recent(&self, earliest_time: zx::Time) -> Vec<ConnectFailure> {
        self.0.iter().skip_while(|denial| denial.time < earliest_time).cloned().collect()
    }
}

/// Unexpected disconnects, either from the AP sending a disconnect or a loss of signal.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Disconnect {
    /// The time of the disconnect, used to determe whether this disconnect is still relevant.
    pub time: zx::Time,
    /// The BSSID that we only had a short connection uptime on.
    pub bssid: client_types::Bssid,
    /// The time between connection starting and disconnecting.
    pub uptime: zx::Duration,
}

#[derive(Clone, Debug, PartialEq)]
pub struct DisconnectList(VecDeque<Disconnect>);

impl DisconnectList {
    pub fn new() -> Self {
        Self(VecDeque::with_capacity(NUM_DISCONNECTS))
    }

    /// Add the disconnect, dropping the oldest value if the list is already full.
    pub fn add(&mut self, bssid: client_types::Bssid, uptime: zx::Duration, curr_time: zx::Time) {
        if self.0.len() == self.0.capacity() {
            let _ = self.0.pop_front();
        }
        self.0.push_back(Disconnect { time: curr_time, bssid, uptime });
    }

    pub fn get_recent(&self, earliest_time: zx::Time) -> Vec<Disconnect> {
        self.0.iter().skip_while(|d| d.time < earliest_time).cloned().collect()
    }
}

/// Used to allow hidden probability calculations to make use of what happened most recently
#[derive(Clone, Copy)]
pub enum HiddenProbEvent {
    /// We just saw the network in a passive scan
    SeenPassive,
    /// We just connected to the network using passive scan results
    ConnectPassive,
    /// We just connected to the network after needing an active scan to see it.
    ConnectActive,
    /// We just actively scanned for the network and did not see it.
    NotSeenActive,
}

/// Saved data for networks, to remember how to connect to a network and determine if we should.
#[derive(Clone, Debug, PartialEq)]
pub struct NetworkConfig {
    /// (persist) SSID and security type to identify a network.
    pub ssid: client_types::Ssid,
    pub security_type: SecurityType,
    /// (persist) Credential to connect to a protected network or None if the network is open.
    pub credential: Credential,
    /// (persist) Remember whether our network indentifier and credential work.
    pub has_ever_connected: bool,
    /// How confident we are that this network is hidden, between 0 and 1. We will use
    /// this number to probabilistically perform an active scan for the network. This is persisted
    /// to maintain consistent behavior between reboots. 0 means not hidden.
    pub hidden_probability: f32,
    /// Data that we use to calculate hidden_probability.
    hidden_probability_stats: HiddenProbabilityStats,
    /// Used to estimate quality to determine whether we want to choose this network.
    pub perf_stats: PerformanceStats,
}

impl NetworkConfig {
    /// A new network config is created by loading from persistent storage on boot or when a new
    /// network is saved.
    pub fn new(
        id: NetworkIdentifier,
        credential: Credential,
        has_ever_connected: bool,
    ) -> Result<Self, NetworkConfigError> {
        check_config_errors(&id.ssid, &id.security_type, &credential)?;

        Ok(Self {
            ssid: id.ssid,
            security_type: id.security_type,
            credential,
            has_ever_connected,
            hidden_probability: PROB_HIDDEN_DEFAULT,
            hidden_probability_stats: HiddenProbabilityStats::new(),
            perf_stats: PerformanceStats::new(),
        })
    }

    // Update the network config's probability that we will actively scan for the network.
    // If a network has been both seen in a passive scan and connected to after an active scan,
    // we will determine probability based on what happened most recently.
    // TODO(63306) Add metric to see if we see conflicting passive/active events.
    pub fn update_hidden_prob(&mut self, event: HiddenProbEvent) {
        match event {
            HiddenProbEvent::ConnectPassive => {
                self.hidden_probability = PROB_HIDDEN_IF_CONNECT_PASSIVE;
            }
            HiddenProbEvent::SeenPassive => {
                // If the probability hidden is lower from connecting to the network after a
                // passive scan, don't change.
                if self.hidden_probability > PROB_HIDDEN_IF_SEEN_PASSIVE {
                    self.hidden_probability = PROB_HIDDEN_IF_SEEN_PASSIVE;
                }
            }
            HiddenProbEvent::ConnectActive => {
                self.hidden_probability_stats.connected_active = true;
                self.hidden_probability = PROB_HIDDEN_IF_CONNECT_ACTIVE;
            }
            HiddenProbEvent::NotSeenActive => {
                // If we have previously required an active scan to connect this network, we are
                // confident that it is hidden and don't care about this event.
                if self.hidden_probability_stats.connected_active {
                    return;
                }
                // The probability will not be changed if already lower than the threshold.
                if self.hidden_probability <= PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE {
                    return;
                }
                // If we failed to find the network in an active scan, lower the probability but
                // not below a certain threshold.
                let new_prob = self.hidden_probability - PROB_HIDDEN_INCREMENT_NOT_SEEN_ACTIVE;
                self.hidden_probability = new_prob.max(PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE);
            }
        }
    }
}

impl From<&NetworkConfig> for fidl_policy::NetworkConfig {
    fn from(network_config: &NetworkConfig) -> Self {
        let network_id = fidl_policy::NetworkIdentifier {
            ssid: network_config.ssid.to_vec(),
            type_: network_config.security_type.clone().into(),
        };
        let credential = network_config.credential.clone().into();
        fidl_policy::NetworkConfig {
            id: Some(network_id),
            credential: Some(credential),
            ..fidl_policy::NetworkConfig::EMPTY
        }
    }
}

/// The credential of a network connection. It mirrors the fidl_fuchsia_wlan_policy Credential
#[derive(Arbitrary)] // Derive Arbitrary for fuzzer
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Credential {
    None,
    Password(Vec<u8>),
    Psk(Vec<u8>),
}

impl Credential {
    /// Returns:
    /// - an Open-Credential instance iff `bytes` is empty,
    /// - a Password-Credential in all other cases.
    /// This function does not support reading PSK from bytes because the PSK byte length overlaps
    /// with a valid password length. This function should only be used to load legacy data, where
    /// PSK was not supported.
    /// Note: This function is of temporary nature to support legacy code.
    pub fn from_bytes(bytes: impl AsRef<[u8]> + Into<Vec<u8>>) -> Self {
        match bytes.as_ref().len() {
            0 => Credential::None,
            _ => Credential::Password(bytes.into()),
        }
    }

    /// Transform credential into the bytes that represent the credential, dropping the information
    /// of the type. This is used to support the legacy storage method.
    pub fn into_bytes(self) -> Vec<u8> {
        match self {
            Credential::Password(pwd) => pwd,
            Credential::Psk(psk) => psk,
            Credential::None => vec![],
        }
    }

    /// Choose a security type that fits the credential while we don't actually know the security type
    /// of the saved networks. This should only be used if we don't have a specified security type.
    pub fn derived_security_type(&self) -> SecurityType {
        match self {
            Credential::None => SecurityType::None,
            _ => SecurityType::Wpa2,
        }
    }
}

impl TryFrom<fidl_policy::Credential> for Credential {
    type Error = NetworkConfigError;
    /// Create a Credential from a fidl Crednetial value.
    fn try_from(credential: fidl_policy::Credential) -> Result<Self, Self::Error> {
        match credential {
            fidl_policy::Credential::None(fidl_policy::Empty {}) => Ok(Self::None),
            fidl_policy::Credential::Password(pwd) => Ok(Self::Password(pwd)),
            fidl_policy::Credential::Psk(psk) => Ok(Self::Psk(psk)),
            _ => Err(NetworkConfigError::CredentialTypeInvalid),
        }
    }
}

impl From<Credential> for fidl_policy::Credential {
    fn from(credential: Credential) -> Self {
        match credential {
            Credential::Password(pwd) => fidl_policy::Credential::Password(pwd),
            Credential::Psk(psk) => fidl_policy::Credential::Psk(psk),
            Credential::None => fidl_policy::Credential::None(fidl_policy::Empty),
        }
    }
}

#[derive(Arbitrary)] // Derive Arbitrary for fuzzer
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub enum SecurityType {
    None,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
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

impl From<SecurityType> for fidl_policy::SecurityType {
    fn from(security_type: SecurityType) -> Self {
        match security_type {
            SecurityType::None => fidl_policy::SecurityType::None,
            SecurityType::Wep => fidl_policy::SecurityType::Wep,
            SecurityType::Wpa => fidl_policy::SecurityType::Wpa,
            SecurityType::Wpa2 => fidl_policy::SecurityType::Wpa2,
            SecurityType::Wpa3 => fidl_policy::SecurityType::Wpa3,
        }
    }
}

/// The network identifier is the SSID and security policy of the network, and it is used to
/// distinguish networks. It mirrors the NetworkIdentifier in fidl_fuchsia_wlan_policy.
#[derive(Arbitrary)] // Derive Arbitrary for fuzzer
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct NetworkIdentifier {
    pub ssid: client_types::Ssid,
    pub security_type: SecurityType,
}

impl NetworkIdentifier {
    pub fn new(ssid: impl Into<client_types::Ssid>, security_type: SecurityType) -> Self {
        NetworkIdentifier { ssid: ssid.into(), security_type }
    }
}

impl From<fidl_policy::NetworkIdentifier> for NetworkIdentifier {
    fn from(id: fidl_policy::NetworkIdentifier) -> Self {
        Self::new(id.ssid, id.type_.into())
    }
}

impl From<NetworkIdentifier> for fidl_policy::NetworkIdentifier {
    fn from(id: NetworkIdentifier) -> Self {
        fidl_policy::NetworkIdentifier { ssid: id.ssid.into(), type_: id.security_type.into() }
    }
}

impl From<NetworkConfig> for fidl_policy::NetworkConfig {
    fn from(config: NetworkConfig) -> Self {
        let network_id = NetworkIdentifier::new(config.ssid, config.security_type);
        fidl_policy::NetworkConfig {
            id: Some(fidl_policy::NetworkIdentifier::from(network_id)),
            credential: Some(fidl_policy::Credential::from(config.credential)),
            ..fidl_policy::NetworkConfig::EMPTY
        }
    }
}

/// Returns an error if the input network values are not valid or none if the values are valid.
/// For example it is an error if the network is Open (no password) but a password is supplied.
/// TODO(nmccracken) - Specific errors need to be added to the API and returned here
fn check_config_errors(
    ssid: &client_types::Ssid,
    security_type: &SecurityType,
    credential: &Credential,
) -> Result<(), NetworkConfigError> {
    // Verify SSID has at most fidl_ieee80211::MAX_SSID_BYTE_LEN bytes
    if ssid.len() < 1 as usize {
        return Err(NetworkConfigError::SsidEmpty);
    } else if ssid.len() > (fidl_ieee80211::MAX_SSID_BYTE_LEN as usize) {
        return Err(NetworkConfigError::SsidExceedsMaxLen);
    }
    // Verify that credentials match the security type. This code only inspects the lengths of
    // passphrases and PSKs; the underlying data is considered opaque here.
    match security_type {
        SecurityType::None => {
            if let Credential::Psk(_) | Credential::Password(_) = credential {
                return Err(NetworkConfigError::OpenNetworkPassword);
            }
        }
        // Note that some vendors allow WEP passphrase and PSK lengths that are not described by
        // IEEE 802.11. These lengths are unsupported. See also the `wep_deprecated` crate.
        SecurityType::Wep => match credential {
            Credential::Password(ref password) => match password.len() {
                // ASCII encoding.
                WEP_40_ASCII_LEN | WEP_104_ASCII_LEN => {}
                // Hexadecimal encoding.
                WEP_40_HEX_LEN | WEP_104_HEX_LEN => {}
                _ => {
                    return Err(NetworkConfigError::PasswordLen);
                }
            },
            _ => {
                return Err(NetworkConfigError::MissingPasswordPsk);
            }
        },
        SecurityType::Wpa | SecurityType::Wpa2 | SecurityType::Wpa3 => match credential {
            Credential::Password(ref pwd) => {
                if pwd.len() < WPA_MIN_PASSWORD_LEN || pwd.len() > WPA_MAX_PASSWORD_LEN {
                    return Err(NetworkConfigError::PasswordLen);
                }
            }
            Credential::Psk(ref psk) => {
                if psk.len() != WPA_PSK_BYTE_LEN {
                    return Err(NetworkConfigError::PskLen);
                }
            }
            _ => {
                return Err(NetworkConfigError::MissingPasswordPsk);
            }
        },
    }
    Ok(())
}

/// Error codes representing problems in trying to save a network config, such as errors saving
/// or removing a network config, or for invalid values when trying to create a network config.
pub enum NetworkConfigError {
    OpenNetworkPassword,
    PasswordLen,
    PskLen,
    SsidEmpty,
    SsidExceedsMaxLen,
    MissingPasswordPsk,
    ConfigMissingId,
    ConfigMissingCredential,
    CredentialTypeInvalid,
    StashWriteError,
    LegacyWriteError,
}

impl Debug for NetworkConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        match self {
            NetworkConfigError::OpenNetworkPassword => {
                write!(f, "can't have an open network with a password or PSK")
            }
            NetworkConfigError::PasswordLen => write!(f, "invalid password length"),
            NetworkConfigError::PskLen => write!(f, "invalid PSK length"),
            NetworkConfigError::SsidEmpty => {
                write!(f, "SSID must have a non-zero length")
            }
            NetworkConfigError::SsidExceedsMaxLen => {
                write!(f, "SSID has max allowed length of {}", fidl_ieee80211::MAX_SSID_BYTE_LEN)
            }
            NetworkConfigError::MissingPasswordPsk => {
                write!(f, "no password or PSK provided but required by security type")
            }
            NetworkConfigError::ConfigMissingId => {
                write!(f, "cannot create network config, network id is None")
            }
            NetworkConfigError::ConfigMissingCredential => {
                write!(f, "cannot create network config, no credential is given")
            }
            NetworkConfigError::CredentialTypeInvalid => {
                write!(f, "cannot convert fidl Credential, unknown variant")
            }
            NetworkConfigError::StashWriteError => {
                write!(f, "error writing network config to stash")
            }
            NetworkConfigError::LegacyWriteError => {
                write!(f, "error writing network config to legacy storage")
            }
        }
    }
}

impl From<NetworkConfigError> for fidl_policy::NetworkConfigChangeError {
    fn from(err: NetworkConfigError) -> Self {
        match err {
            NetworkConfigError::OpenNetworkPassword | NetworkConfigError::MissingPasswordPsk => {
                fidl_policy::NetworkConfigChangeError::InvalidSecurityCredentialError
            }
            NetworkConfigError::PasswordLen | NetworkConfigError::PskLen => {
                fidl_policy::NetworkConfigChangeError::CredentialLenError
            }
            NetworkConfigError::SsidEmpty => fidl_policy::NetworkConfigChangeError::SsidEmptyError,
            NetworkConfigError::SsidExceedsMaxLen => {
                fidl_policy::NetworkConfigChangeError::SsidExceedsMaxLenError
            }
            NetworkConfigError::ConfigMissingId | NetworkConfigError::ConfigMissingCredential => {
                fidl_policy::NetworkConfigChangeError::NetworkConfigMissingFieldError
            }
            NetworkConfigError::CredentialTypeInvalid => {
                fidl_policy::NetworkConfigChangeError::UnsupportedCredentialError
            }
            NetworkConfigError::StashWriteError | NetworkConfigError::LegacyWriteError => {
                fidl_policy::NetworkConfigChangeError::NetworkConfigWriteError
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, wlan_common::assert_variant};

    #[fuchsia::test]
    fn new_network_config_none_credential() {
        let credential = Credential::None;
        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::None),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::from("foo"),
                security_type: SecurityType::None,
                credential: credential,
                has_ever_connected: false,
                hidden_probability: PROB_HIDDEN_DEFAULT,
                hidden_probability_stats: HiddenProbabilityStats::new(),
                perf_stats: PerformanceStats::new()
            }
        );
    }

    #[fuchsia::test]
    fn new_network_config_password_credential() {
        let credential = Credential::Password(b"foo-password".to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::from("foo"),
                security_type: SecurityType::Wpa2,
                credential: credential,
                has_ever_connected: false,
                hidden_probability: PROB_HIDDEN_DEFAULT,
                hidden_probability_stats: HiddenProbabilityStats::new(),
                perf_stats: PerformanceStats::new()
            }
        );
        assert!(network_config.perf_stats.failure_list.0.is_empty());
    }

    #[fuchsia::test]
    fn new_network_config_psk_credential() {
        let credential = Credential::Psk([1; WPA_PSK_BYTE_LEN].to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::from("foo"),
                security_type: SecurityType::Wpa2,
                credential: credential,
                has_ever_connected: false,
                hidden_probability: PROB_HIDDEN_DEFAULT,
                hidden_probability_stats: HiddenProbabilityStats::new(),
                perf_stats: PerformanceStats::new()
            }
        );
    }

    #[fuchsia::test]
    fn new_network_config_ssid_exceeds_max_length() {
        let credential = Credential::None;

        let config_result = NetworkConfig::new(
            NetworkIdentifier::new([1; 33], SecurityType::Wpa2),
            credential,
            false,
        );

        assert_variant!(config_result, Err(NetworkConfigError::SsidExceedsMaxLen));
    }

    #[fuchsia::test]
    fn new_network_config_invalid_password() {
        let credential = Credential::Password([1; 64].to_vec());

        let config_result =
            NetworkConfig::new(NetworkIdentifier::new("foo", SecurityType::Wpa), credential, false);

        assert_variant!(config_result, Err(NetworkConfigError::PasswordLen));
    }

    #[fuchsia::test]
    fn new_network_config_invalid_psk() {
        let credential = Credential::Psk(b"bar".to_vec());

        let config_result = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential,
            false,
        );

        assert_variant!(config_result, Err(NetworkConfigError::PskLen));
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_wep_password() {
        // Unsupported length (7).
        let password = Credential::Password(b"1234567".to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wep,
                &password
            ),
            Err(NetworkConfigError::PasswordLen)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_wpa_password() {
        // password too short
        let short_password = Credential::Password(b"1234567".to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa2,
                &short_password
            ),
            Err(NetworkConfigError::PasswordLen)
        );

        // password too long
        let long_password = Credential::Password([5, 65].to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa2,
                &long_password
            ),
            Err(NetworkConfigError::PasswordLen)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_wep_credential_variant() {
        // Unsupported variant (`Psk`).
        let psk = Credential::Psk(b"12345".to_vec());
        assert_variant!(
            check_config_errors(&client_types::Ssid::from("valid_ssid"), &SecurityType::Wep, &psk),
            Err(NetworkConfigError::MissingPasswordPsk)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_wpa_psk() {
        // PSK length not 32 characters
        let short_psk = Credential::Psk([6; WPA_PSK_BYTE_LEN - 1].to_vec());

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa2,
                &short_psk
            ),
            Err(NetworkConfigError::PskLen)
        );

        let long_psk = Credential::Psk([7; WPA_PSK_BYTE_LEN + 1].to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa2,
                &long_psk
            ),
            Err(NetworkConfigError::PskLen)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_security_credential() {
        // Use a password with open network.
        let password = Credential::Password(b"password".to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::None,
                &password
            ),
            Err(NetworkConfigError::OpenNetworkPassword)
        );

        let psk = Credential::Psk([1; WPA_PSK_BYTE_LEN].to_vec());
        assert_variant!(
            check_config_errors(&client_types::Ssid::from("valid_ssid"), &SecurityType::None, &psk),
            Err(NetworkConfigError::OpenNetworkPassword)
        );
        // Use no password with a protected network.
        let password = Credential::None;
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa,
                &password
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa2,
                &password
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::from("valid_ssid"),
                &SecurityType::Wpa3,
                &password
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_ssid_empty() {
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::empty(),
                &SecurityType::None,
                &Credential::None
            ),
            Err(NetworkConfigError::SsidEmpty)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_ssid_exceeds_max_length() {
        // The longest valid SSID length is 32, so 33 characters is too long.
        let long_ssid = client_types::Ssid::from([6; 33]);
        assert_variant!(
            check_config_errors(&long_ssid, &SecurityType::None, &Credential::None),
            Err(NetworkConfigError::SsidExceedsMaxLen)
        );
    }

    #[fuchsia::test]
    fn failure_list_add_and_get() {
        let mut failure_list = ConnectFailureList::new();

        // Get time before adding so we can get back everything we added.
        let curr_time = zx::Time::get_monotonic();
        assert!(failure_list.get_recent(curr_time).is_empty());
        let bssid = client_types::Bssid([1; 6]);
        failure_list.add(bssid.clone(), FailureReason::GeneralFailure);

        let result_list = failure_list.get_recent(curr_time);
        assert_eq!(1, result_list.len());
        assert_eq!(FailureReason::GeneralFailure, result_list[0].reason);
        assert_eq!(bssid, result_list[0].bssid);
        // Should not get any results if we request denials older than the specified time.
        let later_time = zx::Time::get_monotonic();
        assert!(failure_list.get_recent(later_time).is_empty());
    }

    #[fuchsia::test]
    fn failure_list_add_when_full() {
        let mut failure_list = ConnectFailureList::new();

        let curr_time = zx::Time::get_monotonic();
        assert!(failure_list.get_recent(curr_time).is_empty());
        let failure_list_capacity = failure_list.0.capacity();
        assert!(failure_list_capacity >= NUM_DENY_REASONS);
        let bssid = client_types::Bssid([0; 6]);
        for _i in 0..failure_list_capacity + 2 {
            failure_list.add(bssid, FailureReason::GeneralFailure);
        }
        // Since we do not know time of each entry in the list, check the other values and length
        assert_eq!(failure_list_capacity, failure_list.get_recent(curr_time).len());
        for failure in failure_list.get_recent(curr_time) {
            assert_eq!(FailureReason::GeneralFailure, failure.reason);
            assert_eq!(bssid, failure.bssid)
        }
    }

    #[fuchsia::test]
    fn get_part_of_failure_list() {
        let mut failure_list = ConnectFailureList::new();
        let bssid = client_types::Bssid([0; 6]);
        // curr_time is before or at the part of the list we want and after the one we don't want
        let curr_time = zx::Time::get_monotonic();
        // Inject a failure into the list that is older than the specified time.
        let old_time = curr_time - zx::Duration::from_seconds(1);
        failure_list.0.push_back(ConnectFailure {
            reason: FailureReason::GeneralFailure,
            time: old_time,
            bssid: bssid.clone(),
        });

        // Choose half capacity to get so that we know the previous one is still in list
        let half_capacity = failure_list.0.capacity() / 2;
        for _ in 0..half_capacity {
            failure_list.add(bssid.clone(), FailureReason::GeneralFailure);
        }

        // Since we do not know time of each entry in the list, check the other values and length
        assert_eq!(half_capacity, failure_list.get_recent(curr_time).len());
        for denial in failure_list.get_recent(curr_time) {
            assert_eq!(FailureReason::GeneralFailure, denial.reason);
        }

        // Add one more and check list again
        failure_list.add(bssid.clone(), FailureReason::GeneralFailure);
        assert_eq!(half_capacity + 1, failure_list.get_recent(curr_time).len());
        for failure in failure_list.get_recent(curr_time) {
            assert_eq!(FailureReason::GeneralFailure, failure.reason);
            assert_eq!(bssid, failure.bssid);
        }
    }

    #[fuchsia::test]
    fn test_disconnect_list_add_and_get() {
        let mut disconnects = DisconnectList::new();

        let curr_time = zx::Time::get_monotonic();
        assert!(disconnects.get_recent(curr_time).is_empty());
        let bssid = client_types::Bssid([1; 6]);
        let uptime = zx::Duration::from_seconds(2);

        // Add a disconnect and check that we get it back.
        disconnects.add(bssid, uptime, curr_time);

        // We should get back the added disconnect when specifying the same or an earlier time.
        let expected_disconnect = Disconnect { bssid, uptime, time: curr_time };
        assert_eq!(disconnects.get_recent(curr_time).len(), 1);
        assert_variant!(disconnects.get_recent(curr_time).as_slice(), [d] => {
            assert_eq!(d, &expected_disconnect.clone());
        });
        let earlier_time = curr_time - zx::Duration::from_seconds(1);
        assert_variant!(disconnects.get_recent(earlier_time).as_slice(), [d] => {
            assert_eq!(d, &expected_disconnect.clone());
        });
        // The results should be empty if the requested time is after the disconnect's time.
        // The disconnect is considered stale.
        let later_time = curr_time + zx::Duration::from_seconds(1);
        assert!(disconnects.get_recent(later_time).is_empty());
    }

    #[fuchsia::test]
    fn test_disconnect_list_add_removes_oldest_when_full() {
        let mut disconnects = DisconnectList::new();

        assert!(disconnects.get_recent(zx::Time::ZERO).is_empty());
        let disconnect_list_capacity = disconnects.0.capacity();
        // VecDequeue::with_capacity allocates at least the specified amount, not necessarily
        // equal to the specified amount.
        assert!(disconnect_list_capacity >= NUM_DISCONNECTS);

        // Insert first disconnect, which we will check was pushed out of the list
        let first_bssid = client_types::Bssid([10; 6]);
        disconnects.add(first_bssid, zx::Duration::from_seconds(1), zx::Time::get_monotonic());
        assert_variant!(disconnects.get_recent(zx::Time::ZERO).as_slice(), [d] => {
            assert_eq!(d.bssid, first_bssid);
        });

        let bssid = client_types::Bssid([0; 6]);
        for _i in 0..disconnect_list_capacity {
            disconnects.add(bssid, zx::Duration::from_seconds(2), zx::Time::get_monotonic());
        }
        let all_disconnects = disconnects.get_recent(zx::Time::ZERO);
        for d in &all_disconnects {
            assert_eq!(d.bssid, bssid);
        }
        assert_eq!(all_disconnects.len(), disconnect_list_capacity);
    }

    #[fuchsia::test]
    fn test_credential_from_bytes() {
        assert_eq!(Credential::from_bytes(vec![1]), Credential::Password(vec![1]));
        assert_eq!(Credential::from_bytes(vec![2; 63]), Credential::Password(vec![2; 63]));
        // credential from bytes should only be used to load legacy data, so PSK won't be supported
        assert_eq!(
            Credential::from_bytes(vec![2; WPA_PSK_BYTE_LEN]),
            Credential::Password(vec![2; WPA_PSK_BYTE_LEN])
        );
        assert_eq!(Credential::from_bytes(vec![]), Credential::None);
    }

    #[fuchsia::test]
    fn test_derived_security_type_from_credential() {
        let password = Credential::Password(b"password".to_vec());
        let psk = Credential::Psk(b"psk-type".to_vec());
        let none = Credential::None;

        assert_eq!(SecurityType::Wpa2, password.derived_security_type());
        assert_eq!(SecurityType::Wpa2, psk.derived_security_type());
        assert_eq!(SecurityType::None, none.derived_security_type());
    }

    #[fuchsia::test]
    fn test_hidden_prob_calculation() {
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_DEFAULT);

        network_config.update_hidden_prob(HiddenProbEvent::SeenPassive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);

        network_config.update_hidden_prob(HiddenProbEvent::ConnectPassive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_PASSIVE);

        // Hidden probability shouldn't go back up after seeing a network in a passive
        // scan again after connecting with a passive scan
        network_config.update_hidden_prob(HiddenProbEvent::SeenPassive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_PASSIVE);
    }

    #[fuchsia::test]
    fn test_hidden_prob_calc_active_connect() {
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");

        network_config.update_hidden_prob(HiddenProbEvent::ConnectActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);

        // If we see a network in a passive scan after connecting from an active scan,
        // we won't care that we previously needed an active scan.
        network_config.update_hidden_prob(HiddenProbEvent::SeenPassive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_SEEN_PASSIVE);

        // If we require an active scan to connect to a network, raise probability as if the
        // network has become hidden.
        network_config.update_hidden_prob(HiddenProbEvent::ConnectActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);
    }

    #[fuchsia::test]
    fn test_hidden_prob_calc_not_seen_in_active_scan_lowers_prob() {
        // Test that updating hidden probability after not seeing the network in a directed active
        // scan lowers the hidden probability
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");

        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        let expected_prob = PROB_HIDDEN_DEFAULT - PROB_HIDDEN_INCREMENT_NOT_SEEN_ACTIVE;
        assert_eq!(network_config.hidden_probability, expected_prob);

        // If we update hidden probability again, the probability should lower again.
        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        let expected_prob = expected_prob - PROB_HIDDEN_INCREMENT_NOT_SEEN_ACTIVE;
        assert_eq!(network_config.hidden_probability, expected_prob);
    }

    #[fuchsia::test]
    fn test_hidden_prob_calc_not_seen_in_active_scan_does_not_lower_past_threshold() {
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");

        // If hidden probability is slightly above the minimum from not seing the network in an
        // active scan, it should not be lowered past the minimum.
        network_config.hidden_probability = PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE + 0.01;
        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE);

        // If hidden probability is at the minimum, it should not be lowered.
        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE);
    }

    #[fuchsia::test]
    fn test_hidden_prob_calc_not_seen_in_active_scan_does_not_change_if_lower_than_threshold() {
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");

        // If the hidden probability is lower than the minimum of not seeing the network in an,
        // active scan, which could happen after seeing it in a passive scan, the hidden
        // probability will not lower from this event.
        let prob_before_update = PROB_HIDDEN_MIN_FROM_NOT_SEEN_ACTIVE - 0.1;
        network_config.hidden_probability = prob_before_update;
        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        assert_eq!(network_config.hidden_probability, prob_before_update);
    }

    #[fuchsia::test]
    fn test_hidden_prob_calc_not_seen_active_after_active_connect() {
        // Test the specific case where we fail to see the network in an active scan after we
        // previously connected to the network after an active scan was required.
        let mut network_config = NetworkConfig::new(
            NetworkIdentifier::new("some_ssid", SecurityType::None),
            Credential::None,
            false,
        )
        .expect("Failed to create network config");

        network_config.update_hidden_prob(HiddenProbEvent::ConnectActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);

        // If we update the probability after a not-seen-in-active-scan, the probability should
        // still reflect that we think the network is hidden after the connect.
        network_config.update_hidden_prob(HiddenProbEvent::NotSeenActive);
        assert_eq!(network_config.hidden_probability, PROB_HIDDEN_IF_CONNECT_ACTIVE);
    }
}
