// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
use fidl_fuchsia_wlan_common_security as fidl_security;
use {
    crate::client::{bss_selection::SignalData, types as client_types},
    arbitrary::Arbitrary,
    fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync, fuchsia_zircon as zx,
    std::{
        cmp::Reverse,
        collections::{HashMap, HashSet, VecDeque},
        convert::TryFrom,
        fmt::{self, Debug},
    },
    wlan_common::security::{
        wep::WepKey,
        wpa::{
            credential::{Passphrase, Psk},
            WpaDescriptor,
        },
        SecurityAuthenticator, SecurityDescriptor,
    },
};

/// The max number of connection results we will store per BSS at a time. For now, this number is
/// chosen arbitartily.
const NUM_CONNECTION_RESULTS_PER_BSS: usize = 10;
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
    pub connect_failures: ConnectFailuresByBssid,
    pub past_connections: PastConnectionsByBssid,
}

impl PerformanceStats {
    pub fn new() -> Self {
        Self {
            connect_failures: ConnectFailuresByBssid::new(),
            past_connections: PastConnectionsByBssid::new(),
        }
    }
}

/// Trait for time function, for use in HistoricalList get_recent functions and AddAndGetRecent
/// associated type
pub trait Time {
    fn time(&self) -> fasync::Time;
}

/// Trait for use in HistoricalListsByBssid generic
pub trait AddAndGetRecent {
    type Data;

    fn add(&mut self, historical_data: Self::Data);
    fn get_recent(&self, earliest_time: fasync::Time) -> Vec<Self::Data>;
}

/// Generic struct for list that stores historical data in a VecDeque, up to the some number of most
/// recent entries.
#[derive(Clone, Debug, PartialEq)]
pub struct HistoricalList<T: Time>(VecDeque<T>);

impl<T> HistoricalList<T>
where
    T: Time + Clone,
{
    pub fn new() -> Self {
        Self(VecDeque::with_capacity(NUM_CONNECTION_RESULTS_PER_BSS))
    }
}

impl<T> AddAndGetRecent for HistoricalList<T>
where
    T: Time + Clone,
{
    type Data = T;

    /// Add a new entry, purging the oldest if at capacity
    fn add(&mut self, historical_data: T) {
        if self.0.len() == self.0.capacity() {
            let _ = self.0.pop_front();
        }
        self.0.push_back(historical_data);
    }

    /// Retrieve list of entries with a time more recent than earliest_time, sorted from oldest to
    /// newest. May be empty.
    fn get_recent(&self, earliest_time: fasync::Time) -> Vec<T> {
        let i = self.0.partition_point(|data| data.time() < earliest_time);
        return self.0.iter().skip(i).cloned().collect();
    }
}

impl<T> Default for HistoricalList<T>
where
    T: Time + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

/// Generic struct for map from BSSID to HistoricalList
#[derive(Clone, Debug, PartialEq)]
pub struct HistoricalListsByBssid<List>(HashMap<client_types::Bssid, List>);

impl<Data, List> HistoricalListsByBssid<List>
where
    Data: Time + Clone,
    List: AddAndGetRecent<Data = Data> + Default + Clone,
{
    pub fn new() -> Self {
        Self(HashMap::new())
    }

    pub fn add(&mut self, bssid: client_types::Bssid, data: Data) {
        self.0.entry(bssid).or_default().add(data);
    }

    /// Retrieve list of Data entries to any BSS with a time more recent than earliest_time, sorted
    /// from oldest to newest. May be empty.
    pub fn get_recent_for_network(&self, earliest_time: fasync::Time) -> Vec<Data> {
        let mut recents: Vec<Data> = vec![];
        for bssid in self.0.keys() {
            recents.append(&mut self.get_list_for_bss(bssid).get_recent(earliest_time));
        }
        recents.sort_by(|a, b| a.time().cmp(&b.time()));
        recents
    }

    /// Retrieve List for a particular BSS, in order to retrieve BSS specific Data entries.
    pub fn get_list_for_bss(&self, bssid: &client_types::Bssid) -> List {
        self.0.get(bssid).cloned().unwrap_or_default()
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
    pub time: fasync::Time,
    /// The reason that connection failed
    pub reason: FailureReason,
    /// The BSSID that we failed to connect to
    pub bssid: client_types::Bssid,
}

impl Time for ConnectFailure {
    fn time(&self) -> fasync::Time {
        self.time
    }
}

/// Data points related to historical connection
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct PastConnectionData {
    pub bssid: client_types::Bssid,
    /// Time at which connect was first attempted
    pub connection_attempt_time: fasync::Time,
    /// Duration from connection attempt to success
    pub time_to_connect: zx::Duration,
    /// Time at which the connection was ended
    pub disconnect_time: fasync::Time,
    /// The time that the connection was up - from established to disconnected.
    pub connection_uptime: zx::Duration,
    /// Cause of disconnect or failure to connect
    pub disconnect_reason: client_types::DisconnectReason,
    /// Final signal strength measure before disconnect
    pub signal_data_at_disconnect: SignalData,
    /// Average phy rate over connection duration
    pub average_tx_rate: u32,
}

impl PastConnectionData {
    pub fn new(
        bssid: client_types::Bssid,
        connection_attempt_time: fasync::Time,
        time_to_connect: zx::Duration,
        disconnect_time: fasync::Time,
        connection_uptime: zx::Duration,
        disconnect_reason: client_types::DisconnectReason,
        signal_data_at_disconnect: SignalData,
        average_tx_rate: u32,
    ) -> Self {
        Self {
            bssid,
            connection_attempt_time,
            time_to_connect,
            disconnect_time,
            connection_uptime,
            disconnect_reason,
            signal_data_at_disconnect,
            average_tx_rate,
        }
    }
}

impl Time for PastConnectionData {
    fn time(&self) -> fasync::Time {
        self.disconnect_time
    }
}

/// Data structures for storing historical connection information for a BSS.
pub type ConnectFailureList = HistoricalList<ConnectFailure>;
pub type PastConnectionList = HistoricalList<PastConnectionData>;

/// Data structures storing historical connection information for whole networks (one or more BSSs)
pub type ConnectFailuresByBssid = HistoricalListsByBssid<ConnectFailureList>;
pub type PastConnectionsByBssid = HistoricalListsByBssid<PastConnectionList>;

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
#[derive(Clone, Debug, PartialEq)]
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

    pub fn type_str(&self) -> &str {
        match self {
            Credential::None => "None",
            Credential::Password(_) => "Password",
            Credential::Psk(_) => "PSK",
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

// TODO(fxbug.dev/102606): Remove this operator implementation. Once calls to
//                         `select_authentication_method` are removed from the state machine, there
//                         will instead be an `Authentication` (or `SecurityAuthenticator`) field
//                         in `ScannedCandidate` which can be more directly compared to SME
//                         `ConnectRequest`s in tests.
#[cfg(test)]
impl PartialEq<Option<fidl_security::Credentials>> for Credential {
    fn eq(&self, credentials: &Option<fidl_security::Credentials>) -> bool {
        use fidl_security::{Credentials, WepCredentials, WpaCredentials};

        match credentials {
            None => matches!(self, Credential::None),
            Some(Credentials::Wep(WepCredentials { ref key })) => {
                if let Credential::Password(ref unparsed) = self {
                    // `Credential::Password` is used for both WEP and WPA. The encoding of WEP
                    // keys is unspecified and may be either binary (unencoded) or ASCII-encoded
                    // hexadecimal. To compare, this WEP key must be parsed.
                    WepKey::parse(unparsed).map_or(false, |parsed| &Vec::from(parsed) == key)
                } else {
                    false
                }
            }
            Some(Credentials::Wpa(ref credentials)) => match credentials {
                WpaCredentials::Passphrase(ref passphrase) => {
                    if let Credential::Password(ref unparsed) = self {
                        unparsed == &Vec::from(passphrase.clone())
                    } else {
                        false
                    }
                }
                WpaCredentials::Psk(ref psk) => {
                    if let Credential::Psk(ref unparsed) = self {
                        unparsed == &Vec::from(psk.clone())
                    } else {
                        false
                    }
                }
                _ => panic!("unrecognized FIDL variant"),
            },
            Some(_) => panic!("unrecognized FIDL variant"),
        }
    }
}

// TODO(fxbug.dev/102606): Remove this operator implementation. See the similar conversion above.
#[cfg(test)]
impl PartialEq<Option<Box<fidl_security::Credentials>>> for Credential {
    fn eq(&self, credentials: &Option<Box<fidl_security::Credentials>>) -> bool {
        self.eq(&credentials.as_ref().map(|credentials| *credentials.clone()))
    }
}

#[derive(Arbitrary)] // Derive Arbitrary for fuzzer
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum SecurityType {
    None,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
}

impl From<SecurityDescriptor> for SecurityType {
    fn from(descriptor: SecurityDescriptor) -> Self {
        match descriptor {
            SecurityDescriptor::Open => SecurityType::None,
            SecurityDescriptor::Wep => SecurityType::Wep,
            SecurityDescriptor::Wpa(wpa) => match wpa {
                WpaDescriptor::Wpa1 { .. } => SecurityType::Wpa,
                WpaDescriptor::Wpa2 { .. } => SecurityType::Wpa2,
                WpaDescriptor::Wpa3 { .. } => SecurityType::Wpa3,
            },
        }
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

impl SecurityType {
    /// List all security type variants.
    pub fn list_variants() -> Vec<Self> {
        vec![
            SecurityType::None,
            SecurityType::Wep,
            SecurityType::Wpa,
            SecurityType::Wpa2,
            SecurityType::Wpa3,
        ]
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
    pub fn new(ssid: client_types::Ssid, security_type: SecurityType) -> Self {
        NetworkIdentifier { ssid: ssid.into(), security_type }
    }

    #[cfg(test)]
    pub fn try_from(ssid: &str, security_type: SecurityType) -> Result<Self, anyhow::Error> {
        Ok(NetworkIdentifier { ssid: client_types::Ssid::try_from(ssid)?, security_type })
    }
}

impl From<fidl_policy::NetworkIdentifier> for NetworkIdentifier {
    fn from(id: fidl_policy::NetworkIdentifier) -> Self {
        Self::new(client_types::Ssid::from_bytes_unchecked(id.ssid), id.type_.into())
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
    // Verify SSID has at least 1 byte.
    if ssid.len() < 1 as usize {
        return Err(NetworkConfigError::SsidEmpty);
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

/// Binds a credential to a security protocol.
///
/// Binding constructs a `SecurityAuthenticator` that can be used to construct an SME
/// `ConnectRequest`. This function is similar to `SecurityDescriptor::bind`, but operates on the
/// Policy `Credential` type, which requires some additional logic to determine how the credential
/// data is interpreted.
///
/// Returns `None` if the given protocol is incompatible with the given credential.
fn bind_credential_to_protocol(
    protocol: SecurityDescriptor,
    credential: &Credential,
) -> Option<SecurityAuthenticator> {
    match protocol {
        SecurityDescriptor::Open => match credential {
            Credential::None => protocol.bind(None).ok(),
            _ => None,
        },
        SecurityDescriptor::Wep => match credential {
            Credential::Password(ref key) => {
                WepKey::parse(key).ok().and_then(|key| protocol.bind(Some(key.into())).ok())
            }
            _ => None,
        },
        SecurityDescriptor::Wpa(wpa) => match wpa {
            WpaDescriptor::Wpa1 { .. } | WpaDescriptor::Wpa2 { .. } => match credential {
                Credential::Password(ref passphrase) => Passphrase::try_from(passphrase.as_slice())
                    .ok()
                    .and_then(|passphrase| protocol.bind(Some(passphrase.into())).ok()),
                Credential::Psk(ref psk) => {
                    Psk::parse(psk).ok().and_then(|psk| protocol.bind(Some(psk.into())).ok())
                }
                _ => None,
            },
            WpaDescriptor::Wpa3 { .. } => match credential {
                Credential::Password(ref passphrase) => Passphrase::try_from(passphrase.as_slice())
                    .ok()
                    .and_then(|passphrase| protocol.bind(Some(passphrase.into())).ok()),
                _ => None,
            },
        },
    }
}

/// Creates a security authenticator based on supported security protocols and credentials.
///
/// The authentication method is chosen based on the general strength of each mutually supported
/// security protocol (the protocols supported by both the local and remote stations) and the
/// compatibility of those protocols with the given credentials.
///
/// Returns `None` if no appropriate authentication method can be selected for the given protocols
/// and credentials.
pub fn select_authentication_method(
    mutual_security_protocols: HashSet<SecurityDescriptor>,
    credential: &Credential,
) -> Option<SecurityAuthenticator> {
    let mut protocols: Vec<_> = mutual_security_protocols.into_iter().collect();
    protocols.sort_by_key(|protocol| {
        Reverse(match protocol {
            SecurityDescriptor::Open => 0,
            SecurityDescriptor::Wep => 1,
            SecurityDescriptor::Wpa(ref wpa) => match wpa {
                WpaDescriptor::Wpa1 { .. } => 2,
                WpaDescriptor::Wpa2 { .. } => 3,
                WpaDescriptor::Wpa3 { .. } => 4,
            },
        })
    });
    protocols
        .into_iter()
        .flat_map(|protocol| bind_credential_to_protocol(protocol, credential))
        .next()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::util::testing::random_connection_data,
        std::iter::FromIterator,
        test_case::test_case,
        wlan_common::assert_variant,
        wlan_common::security::{
            wep::WepAuthenticator,
            wpa::{
                Authentication, Wpa1Credentials, Wpa2PersonalCredentials, Wpa3PersonalCredentials,
                WpaAuthenticator,
            },
        },
    };

    #[fuchsia::test]
    fn new_network_config_none_credential() {
        let credential = Credential::None;
        let network_config = NetworkConfig::new(
            NetworkIdentifier::try_from("foo", SecurityType::None).unwrap(),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::try_from("foo").unwrap(),
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
            NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap(),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::try_from("foo").unwrap(),
                security_type: SecurityType::Wpa2,
                credential: credential,
                has_ever_connected: false,
                hidden_probability: PROB_HIDDEN_DEFAULT,
                hidden_probability_stats: HiddenProbabilityStats::new(),
                perf_stats: PerformanceStats::new()
            }
        );
        assert!(network_config.perf_stats.connect_failures.0.is_empty());
    }

    #[fuchsia::test]
    fn new_network_config_psk_credential() {
        let credential = Credential::Psk([1; WPA_PSK_BYTE_LEN].to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap(),
            credential.clone(),
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(
            network_config,
            NetworkConfig {
                ssid: client_types::Ssid::try_from("foo").unwrap(),
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
    fn new_network_config_invalid_password() {
        let credential = Credential::Password([1; 64].to_vec());

        let config_result = NetworkConfig::new(
            NetworkIdentifier::try_from("foo", SecurityType::Wpa).unwrap(),
            credential,
            false,
        );

        assert_variant!(config_result, Err(NetworkConfigError::PasswordLen));
    }

    #[fuchsia::test]
    fn new_network_config_invalid_psk() {
        let credential = Credential::Psk(b"bar".to_vec());

        let config_result = NetworkConfig::new(
            NetworkIdentifier::try_from("foo", SecurityType::Wpa2).unwrap(),
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
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
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
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::Wpa2,
                &short_password
            ),
            Err(NetworkConfigError::PasswordLen)
        );

        // password too long
        let long_password = Credential::Password([5, 65].to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
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
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::Wep,
                &psk
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );
    }

    #[fuchsia::test]
    fn check_config_errors_invalid_wpa_psk() {
        // PSK length not 32 characters
        let short_psk = Credential::Psk([6; WPA_PSK_BYTE_LEN - 1].to_vec());

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::Wpa2,
                &short_psk
            ),
            Err(NetworkConfigError::PskLen)
        );

        let long_psk = Credential::Psk([7; WPA_PSK_BYTE_LEN + 1].to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
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
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::None,
                &password
            ),
            Err(NetworkConfigError::OpenNetworkPassword)
        );

        let psk = Credential::Psk([1; WPA_PSK_BYTE_LEN].to_vec());
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::None,
                &psk
            ),
            Err(NetworkConfigError::OpenNetworkPassword)
        );
        // Use no password with a protected network.
        let password = Credential::None;
        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::Wpa,
                &password
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
                &SecurityType::Wpa2,
                &password
            ),
            Err(NetworkConfigError::MissingPasswordPsk)
        );

        assert_variant!(
            check_config_errors(
                &client_types::Ssid::try_from("valid_ssid").unwrap(),
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

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_failures_by_bssid_add_and_get() {
        let mut connect_failures = ConnectFailuresByBssid::new();
        let curr_time = fasync::Time::now();

        // Add two failures for BSSID_1
        let bssid_1 = client_types::Bssid([1; 6]);
        let failure_1_bssid_1 = ConnectFailure {
            time: curr_time - zx::Duration::from_seconds(10),
            bssid: bssid_1.clone(),
            reason: FailureReason::GeneralFailure,
        };
        connect_failures.add(bssid_1.clone(), failure_1_bssid_1.clone());

        let failure_2_bssid_1 = ConnectFailure {
            time: curr_time - zx::Duration::from_seconds(5),
            bssid: bssid_1.clone(),
            reason: FailureReason::CredentialRejected,
        };
        connect_failures.add(bssid_1.clone(), failure_2_bssid_1.clone());

        // Verify get_recent_for_network(curr_time - 10) retrieves both entries
        assert_eq!(
            connect_failures.get_recent_for_network(curr_time - zx::Duration::from_seconds(10)),
            vec![failure_1_bssid_1, failure_2_bssid_1]
        );

        // Add one failure for BSSID_2
        let bssid_2 = client_types::Bssid([2; 6]);
        let failure_1_bssid_2 = ConnectFailure {
            time: curr_time - zx::Duration::from_seconds(3),
            bssid: bssid_2.clone(),
            reason: FailureReason::GeneralFailure,
        };
        connect_failures.add(bssid_2.clone(), failure_1_bssid_2.clone());

        // Verify get_recent_for_network(curr_time - 10) includes entries from both BSSIDs
        assert_eq!(
            connect_failures.get_recent_for_network(curr_time - zx::Duration::from_seconds(10)),
            vec![failure_1_bssid_1, failure_2_bssid_1, failure_1_bssid_2]
        );

        // Verify get_recent_for_network(curr_time - 9) excludes older entries
        assert_eq!(
            connect_failures.get_recent_for_network(curr_time - zx::Duration::from_seconds(9)),
            vec![failure_2_bssid_1, failure_1_bssid_2]
        );

        // Verify get_recent_for_network(curr_time) is empty
        assert_eq!(connect_failures.get_recent_for_network(curr_time), vec![]);

        // Verify get_list_for_bss retrieves correct ConnectFailureLists
        assert_eq!(
            connect_failures.get_list_for_bss(&bssid_1),
            ConnectFailureList { 0: VecDeque::from_iter([failure_1_bssid_1, failure_2_bssid_1]) }
        );

        assert_eq!(
            connect_failures.get_list_for_bss(&bssid_2),
            ConnectFailureList { 0: VecDeque::from_iter([failure_1_bssid_2]) }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn failure_list_add_and_get() {
        let mut connect_failures = ConnectFailureList::new();

        // Get time before adding so we can get back everything we added.
        let curr_time = fasync::Time::now();
        assert!(connect_failures.get_recent(curr_time).is_empty());
        let bssid = client_types::Bssid([1; 6]);
        let failure =
            ConnectFailure { time: curr_time, bssid, reason: FailureReason::GeneralFailure };
        connect_failures.add(failure);

        let result_list = connect_failures.get_recent(curr_time);
        assert_eq!(1, result_list.len());
        assert_eq!(FailureReason::GeneralFailure, result_list[0].reason);
        assert_eq!(bssid, result_list[0].bssid);
        // Should not get any results if we request denials older than the specified time.
        let later_time = fasync::Time::now();
        assert!(connect_failures.get_recent(later_time).is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_failure_list_add_when_full() {
        let mut connect_failures = ConnectFailureList::new();
        let curr_time = fasync::Time::now();

        // Add to list, exceeding the capacity by one entry
        for i in 0..connect_failures.0.capacity() + 1 {
            connect_failures.add(ConnectFailure {
                time: curr_time + zx::Duration::from_seconds(i as i64),
                reason: FailureReason::GeneralFailure,
                bssid: client_types::Bssid([1; 6]),
            })
        }

        // Validate entry with time = curr_time was evicted.
        for (i, e) in connect_failures.0.iter().enumerate() {
            assert_eq!(e.time, curr_time + zx::Duration::from_seconds(i as i64 + 1));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_past_connections_by_bssid_add_and_get() {
        let mut past_connections_list = PastConnectionsByBssid::new();
        let curr_time = fasync::Time::now();

        // Add two past_connections for BSSID_1
        let mut data_1_bssid_1 = random_connection_data();
        let bssid_1 = data_1_bssid_1.bssid;
        data_1_bssid_1.disconnect_time = curr_time - zx::Duration::from_seconds(10);

        past_connections_list.add(bssid_1.clone(), data_1_bssid_1.clone());

        let mut data_2_bssid_1 = random_connection_data();
        data_2_bssid_1.bssid = bssid_1;
        data_2_bssid_1.disconnect_time = curr_time - zx::Duration::from_seconds(5);
        past_connections_list.add(bssid_1.clone(), data_2_bssid_1.clone());

        // Verify get_recent_for_network(curr_time - 10) retrieves both entries
        assert_eq!(
            past_connections_list
                .get_recent_for_network(curr_time - zx::Duration::from_seconds(10)),
            vec![data_1_bssid_1, data_2_bssid_1]
        );

        // Add one past_connection for BSSID_2
        let mut data_1_bssid_2 = random_connection_data();
        let bssid_2 = data_1_bssid_2.bssid;
        data_1_bssid_2.disconnect_time = curr_time - zx::Duration::from_seconds(3);
        past_connections_list.add(bssid_2.clone(), data_1_bssid_2.clone());

        // Verify get_recent_for_network(curr_time - 10) includes entries from both BSSIDs
        assert_eq!(
            past_connections_list
                .get_recent_for_network(curr_time - zx::Duration::from_seconds(10)),
            vec![data_1_bssid_1, data_2_bssid_1, data_1_bssid_2]
        );

        // Verify get_recent_for_network(curr_time - 9) excludes older entries
        assert_eq!(
            past_connections_list.get_recent_for_network(curr_time - zx::Duration::from_seconds(9)),
            vec![data_2_bssid_1, data_1_bssid_2]
        );

        // Verify get_recent_for_network(curr_time) is empty
        assert_eq!(past_connections_list.get_recent_for_network(curr_time), vec![]);

        // Verify get_list_for_bss retrieves correct PastConnectionLists
        assert_eq!(
            past_connections_list.get_list_for_bss(&bssid_1),
            PastConnectionList { 0: VecDeque::from_iter([data_1_bssid_1, data_2_bssid_1]) }
        );

        assert_eq!(
            past_connections_list.get_list_for_bss(&bssid_2),
            PastConnectionList { 0: VecDeque::from_iter([data_1_bssid_2]) }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_past_connections_list_add_when_full() {
        let mut past_connections_list = PastConnectionList::new();
        let curr_time = fasync::Time::now();

        // Add to list, exceeding the capacity by one entry
        for i in 0..past_connections_list.0.capacity() + 1 {
            let mut data = random_connection_data();
            data.bssid = client_types::Bssid([1; 6]);
            data.disconnect_time = curr_time + zx::Duration::from_seconds(i as i64);
            past_connections_list.add(data);
        }

        // Validate entry with time = curr_time was evicted.
        for (i, e) in past_connections_list.0.iter().enumerate() {
            assert_eq!(e.disconnect_time, curr_time + zx::Duration::from_seconds(i as i64 + 1));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_past_connections_list_add_and_get() {
        let mut past_connections_list = PastConnectionList::new();
        let curr_time = fasync::Time::now();
        assert!(past_connections_list.get_recent(curr_time).is_empty());

        let mut past_connection_data = random_connection_data();
        past_connection_data.disconnect_time = curr_time;
        // Add a past connection
        past_connections_list.add(past_connection_data.clone());

        // We should get back the added data when specifying the same or an earlier time.
        assert_eq!(past_connections_list.get_recent(curr_time).len(), 1);
        assert_variant!(past_connections_list.get_recent(curr_time).as_slice(), [data] => {
            assert_eq!(data, &past_connection_data.clone());
        });
        let earlier_time = curr_time - zx::Duration::from_seconds(1);
        assert_variant!(past_connections_list.get_recent(earlier_time).as_slice(), [data] => {
            assert_eq!(data, &data.clone());
        });
        // The results should be empty if the requested time is after the latest past connection's
        // time.
        let later_time = curr_time + zx::Duration::from_seconds(1);
        assert!(past_connections_list.get_recent(later_time).is_empty());
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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
            NetworkIdentifier::try_from("some_ssid", SecurityType::None).unwrap(),
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

    fn policy_wep_key() -> Credential {
        Credential::Password("abcdef0000".as_bytes().to_vec())
    }

    fn common_wep_key() -> WepKey {
        WepKey::parse("abcdef0000").unwrap()
    }

    fn policy_wpa_password() -> Credential {
        Credential::Password("password".as_bytes().to_vec())
    }

    fn common_wpa_password() -> Passphrase {
        Passphrase::try_from("password").unwrap()
    }

    fn policy_wpa_psk() -> Credential {
        Credential::Psk(vec![0u8; WPA_PSK_BYTE_LEN])
    }

    fn common_wpa_psk() -> Psk {
        Psk::from([0u8; WPA_PSK_BYTE_LEN])
    }

    // Expect successful mapping in the following cases.
    #[test_case(
        [SecurityDescriptor::OPEN],
        Credential::None
        =>
        Some(SecurityAuthenticator::Open)
    )]
    #[test_case(
        [SecurityDescriptor::WEP],
        policy_wep_key()
        =>
        Some(SecurityAuthenticator::Wep(WepAuthenticator {
            key: common_wep_key(),
        }))
    )]
    #[test_case(
        [SecurityDescriptor::WPA1],
        policy_wpa_password()
        =>
        Some(SecurityAuthenticator::Wpa(WpaAuthenticator::Wpa1 {
            credentials: Wpa1Credentials::Passphrase(common_wpa_password()),
        }))
    )]
    #[test_case(
        [SecurityDescriptor::WPA1, SecurityDescriptor::WPA2_PERSONAL],
        policy_wpa_psk()
        =>
        Some(SecurityAuthenticator::Wpa(WpaAuthenticator::Wpa2 {
            cipher: None,
            authentication: Authentication::Personal(
                Wpa2PersonalCredentials::Psk(common_wpa_psk())
            ),
        }))
    )]
    #[test_case(
        [SecurityDescriptor::WPA2_PERSONAL, SecurityDescriptor::WPA3_PERSONAL],
        policy_wpa_password()
        =>
        Some(SecurityAuthenticator::Wpa(WpaAuthenticator::Wpa3 {
            cipher: None,
            authentication: Authentication::Personal(
                Wpa3PersonalCredentials::Passphrase(common_wpa_password())
            ),
        }))
    )]
    #[test_case(
        [SecurityDescriptor::WPA2_PERSONAL],
        policy_wpa_password()
        =>
        Some(SecurityAuthenticator::Wpa(WpaAuthenticator::Wpa2 {
            cipher: None,
            authentication: Authentication::Personal(
                Wpa2PersonalCredentials::Passphrase(common_wpa_password())
            ),
        }))
    )]
    #[test_case(
        [SecurityDescriptor::WPA2_PERSONAL, SecurityDescriptor::WPA3_PERSONAL],
        policy_wpa_psk()
        =>
        Some(SecurityAuthenticator::Wpa(WpaAuthenticator::Wpa2 {
            cipher: None,
            authentication: Authentication::Personal(
                Wpa2PersonalCredentials::Psk(common_wpa_psk())
            ),
        }))
    )]
    // Expect failed mapping in the following cases.
    #[test_case(
        [SecurityDescriptor::WPA3_PERSONAL],
        policy_wpa_psk()
        =>
        None
    )]
    #[fuchsia::test(add_test_attr = false)]
    fn select_authentication_method_matrix(
        mutual_security_protocols: impl IntoIterator<Item = SecurityDescriptor>,
        credential: Credential,
    ) -> Option<SecurityAuthenticator> {
        super::select_authentication_method(
            mutual_security_protocols.into_iter().collect(),
            &credential,
        )
    }

    #[test_case(SecurityType::None)]
    #[test_case(SecurityType::Wep)]
    #[test_case(SecurityType::Wpa)]
    #[test_case(SecurityType::Wpa2)]
    #[test_case(SecurityType::Wpa3)]
    fn test_security_type_list_includes_type(security: SecurityType) {
        let types = SecurityType::list_variants();
        assert!(types.contains(&security));
    }

    // If this test doesn't compile, add the security type to this test and list_security_types().
    #[fuchsia::test]
    fn test_security_type_list_completeness() {
        // Any variant works here.
        let security = SecurityType::Wpa;
        // This will not compile if a new variant is added until this test is updated. Do not
        // a wildcard branch.
        match security {
            SecurityType::None => {}
            SecurityType::Wep => {}
            SecurityType::Wpa => {}
            SecurityType::Wpa2 => {}
            SecurityType::Wpa3 => {}
        }
    }
}
