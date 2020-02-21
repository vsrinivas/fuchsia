// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    std::{collections::VecDeque, time::SystemTime},
    wlan_common::mac::Bssid,
};

/// The maximum number of denied connection reasons we will store for one network at a time.
/// For now this number is chosen arbitrarily.
const NUM_DENY_REASONS: usize = 10;
/// constants for the constraints on valid credential values
const MIN_PASSWORD_LEN: usize = 8;
const MAX_PASSWORD_LEN: usize = 63;
const PSK_LEN: usize = 64;

type SaveError = fidl_policy::NetworkConfigChangeError;

/// History of connects, disconnects, and connection strength to estimate whether we can establish
/// and maintain connection with a network and if it is weakening. Used in choosing best network.
#[derive(Clone, Debug, PartialEq)]
pub struct PerformanceStats {
    /// List of recent connection denials, used to determine whether we should try connecting
    /// to a network again. Capacity of list is at least NUM_DENY_REASONS.
    deny_list: NetworkDenialList,
}

impl PerformanceStats {
    pub fn new() -> Self {
        Self { deny_list: NetworkDenialList::new() }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NetworkDenial {
    /// Remember which AP of a network was denied
    bssid: Bssid,
    /// Determine whether this network denial is still relevant
    time: SystemTime,
    /// The reason that connection was denied
    reason: fidl_sme::ConnectResultCode,
}

/// Ring buffer that holds network denials. It starts empty and replaces oldest when full.
#[derive(Clone, Debug, PartialEq)]
pub struct NetworkDenialList(VecDeque<NetworkDenial>);

impl NetworkDenialList {
    /// The max stored number of deny reasons is at least NUM_DENY_REASONS, decided by VecDeque
    pub fn new() -> Self {
        Self(VecDeque::with_capacity(NUM_DENY_REASONS))
    }

    /// This function will be used in future work when Network Denial reasons are recorded.
    /// Record network denial information in the network config, dropping the oldest information
    /// if the list of denial reasons is already full before adding.
    #[allow(dead_code)]
    pub fn add(&mut self, bssid: Bssid, reason: fidl_sme::ConnectResultCode) {
        if self.0.len() == self.0.capacity() {
            self.0.pop_front();
        }
        self.0.push_back(NetworkDenial { bssid, time: SystemTime::now(), reason });
    }

    /// This function will be used when Network Denial reasons are used to select a network.
    /// Returns a list of the denials that happened at or after given system time.
    #[allow(dead_code)]
    pub fn get_recent(&self, earliest_time: SystemTime) -> Vec<NetworkDenial> {
        self.0.iter().skip_while(|denial| denial.time < earliest_time).cloned().collect()
    }
}

/// Saved data for networks, to remember how to connect to a network and determine if we should.
#[derive(Clone, Debug, PartialEq)]
pub struct NetworkConfig {
    /// (persist) SSID and security type to identify a network.
    pub ssid: Vec<u8>,
    pub security_type: SecurityType,
    /// (persist) Credential to connect to a protected network or None if the network is open.
    pub credential: Credential,
    /// (persist) Remember whether our network indentifier and credential work.
    pub has_ever_connected: bool,
    /// Used to differentiate hidden networks when doing active scans.
    pub seen_in_passive_scan_results: bool,
    /// Used to estimate quality to determine whether we want to choose this network.
    pub perf_stats: PerformanceStats,
}

impl NetworkConfig {
    pub fn new(
        id: NetworkIdentifier,
        credential: Credential,
        has_ever_connected: bool,
        seen_in_passive_scan_results: bool,
    ) -> Result<Self, SaveError> {
        check_config_errors(&id.ssid, &id.security_type, &credential)?;

        Ok(Self {
            ssid: id.ssid,
            security_type: id.security_type,
            credential,
            has_ever_connected,
            seen_in_passive_scan_results,
            perf_stats: PerformanceStats::new(),
        })
    }
}

/// The credential of a network connection. It mirrors the fidl_fuchsia_wlan_policy Credential
#[derive(Clone, Debug, PartialEq)]
pub enum Credential {
    None,
    Password(Vec<u8>),
    Psk(Vec<u8>),
}

/// Returns:
/// - an Open-Credential instance iff `bytes` is empty,
/// - a PSK-Credential instance iff `bytes` holds exactly 64 bytes,
/// - a Password-Credential in all other cases.
/// In the PSK case, the provided bytes must represent the PSK in hex format.
/// Note: This function is of temporary nature until connection results communicate
/// type of credential
impl Credential {
    pub fn from_bytes(bytes: impl AsRef<[u8]> + Into<Vec<u8>>) -> Self {
        const PSK_HEX_STRING_LENGTH: usize = 64;
        match bytes.as_ref().len() {
            0 => Credential::None,
            PSK_HEX_STRING_LENGTH => Credential::Psk(bytes.into()),
            _ => Credential::Password(bytes.into()),
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

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
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
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct NetworkIdentifier {
    pub ssid: Vec<u8>,
    pub security_type: SecurityType,
}

impl NetworkIdentifier {
    pub fn new(ssid: impl Into<Vec<u8>>, security_type: SecurityType) -> Self {
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
        fidl_policy::NetworkIdentifier { ssid: id.ssid, type_: id.security_type.into() }
    }
}

/// Returns an error if the input network values are not valid or none if the values are valid.
/// For example it is an error if the network is Open (no password) but a password is supplied.
/// TODO(nmccracken) - Specific errors need to be added to the API and returned here
fn check_config_errors(
    ssid: impl AsRef<[u8]>,
    security_type: &SecurityType,
    credential: &Credential,
) -> Result<(), SaveError> {
    // Verify SSID has at most 32 characters
    if ssid.as_ref().len() > 32 {
        return Err(SaveError::GeneralError);
    }
    // Verify that credentials match security type
    match security_type {
        SecurityType::None => {
            if let Credential::Psk(_) | Credential::Password(_) = credential {
                return Err(SaveError::GeneralError);
            }
        }
        SecurityType::Wpa | SecurityType::Wpa2 | SecurityType::Wpa3 => match credential {
            Credential::Password(pwd) => {
                if pwd.len() < MIN_PASSWORD_LEN || pwd.len() > MAX_PASSWORD_LEN {
                    return Err(SaveError::GeneralError);
                }
            }
            Credential::Psk(psk) => {
                if psk.len() != PSK_LEN {
                    return Err(SaveError::GeneralError);
                }
            }
            _ => {
                return Err(SaveError::GeneralError);
            }
        },
        _ => {}
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{thread, time::Duration},
        wlan_common::assert_variant,
    };

    #[test]
    fn new_network_config_none_credential() {
        let credential = Credential::None;
        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::None),
            credential,
            false,
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, SecurityType::None);
        assert_eq!(network_config.credential, Credential::None);
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_password_credential() {
        let credential = Credential::Password(b"foo-password".to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential,
            false,
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, SecurityType::Wpa2);
        assert_eq!(network_config.credential, Credential::Password(b"foo-password".to_vec()));
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_psk_credential() {
        let credential = Credential::Psk([1; 64].to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential,
            false,
            false,
        )
        .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, SecurityType::Wpa2);
        assert_eq!(network_config.credential, Credential::Psk([1; 64].to_vec()));
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_invalid_ssid() {
        let credential = Credential::None;

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new([1; 33].to_vec(), SecurityType::Wpa2),
            credential,
            false,
            false,
        );

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn new_network_config_invalid_password() {
        let credential = Credential::Password([1; 64].to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa),
            credential,
            false,
            false,
        );

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn new_network_config_invalid_psk() {
        let credential = Credential::Psk(b"bar".to_vec());

        let network_config = NetworkConfig::new(
            NetworkIdentifier::new("foo", SecurityType::Wpa2),
            credential,
            false,
            false,
        );

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn check_confing_errors_invalid_password() {
        // password too short
        let short_password = Credential::Password(b"1234567".to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa2, &short_password)
        );

        // password too long
        let long_password = Credential::Password([5, 65].to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa2, &long_password)
        );
    }

    #[test]
    fn check_config_errors_invalid_psk() {
        // PSK length not 64 characters
        let short_psk = Credential::Psk([6, 63].to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa2, &short_psk)
        );
        let long_psk = Credential::Psk([7, 65].to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa2, &long_psk)
        );
    }

    #[test]
    fn check_config_errors_invalid_security_credential() {
        // Use a password with open network.
        let password = Credential::Password(b"password".to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::None, &password)
        );
        let psk = Credential::Psk([1, 64].to_vec());
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::None, &psk)
        );

        // Use no password with a protected network.
        let password = Credential::None;
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa, &password)
        );
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa2, &password)
        );
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&b"valid_ssid".to_vec(), &SecurityType::Wpa3, &password)
        );
    }

    #[test]
    fn checkout_config_errors_invalid_ssid() {
        // The longest valid SSID length is 32, so 33 characters is too long.
        let long_ssid = [64; 33].to_vec();
        assert_eq!(
            Err(SaveError::GeneralError),
            check_config_errors(&long_ssid, &SecurityType::None, &Credential::None)
        );
    }

    #[test]
    fn deny_list_add_and_get() {
        let mut deny_list = NetworkDenialList::new();

        // Get time before adding so we can get back everything we added.
        let curr_time = SystemTime::now();
        assert!(deny_list.get_recent(curr_time).is_empty());
        deny_list.add(Bssid([1, 2, 3, 4, 5, 6]), fidl_sme::ConnectResultCode::Failed);

        let result_list = deny_list.get_recent(curr_time);
        assert_eq!(1, result_list.len());
        assert_eq!([1, 2, 3, 4, 5, 6], result_list[0].bssid.0);
        assert_eq!(fidl_sme::ConnectResultCode::Failed, result_list[0].reason);
        // Should not get any results if we request for more recent denials more recent than added.
        assert!(deny_list.get_recent(SystemTime::now() + Duration::new(0, 1)).is_empty());
    }

    #[test]
    fn deny_list_add_when_full() {
        let mut deny_list = NetworkDenialList::new();

        let curr_time = SystemTime::now();
        assert!(deny_list.get_recent(curr_time).is_empty());
        let deny_list_capacity = deny_list.0.capacity();
        assert!(deny_list_capacity >= NUM_DENY_REASONS);
        for _i in 0..deny_list_capacity + 2 {
            deny_list.add(Bssid([1, 2, 3, 4, 5, 6]), fidl_sme::ConnectResultCode::Failed);
        }
        // Since we do not know time of each entry in the list, check the other values and length
        assert_eq!(deny_list_capacity, deny_list.get_recent(curr_time).len());
        for denial in deny_list.get_recent(curr_time) {
            assert_eq!([1, 2, 3, 4, 5, 6], denial.bssid.0);
            assert_eq!(fidl_sme::ConnectResultCode::Failed, denial.reason);
        }
    }

    #[test]
    fn get_part_of_deny_list() {
        let mut deny_list = NetworkDenialList::new();
        deny_list.add(Bssid([6, 5, 4, 3, 2, 1]), fidl_sme::ConnectResultCode::Failed);

        // Choose half capacity to get so that we know the previous one is still in list
        let half_capacity = deny_list.0.capacity() / 2;
        // Ensure we get a time after the deny entry we don't want
        thread::sleep(Duration::new(0, 1));
        // curr_time is before the part of the list we want and after the one we don't want
        let curr_time = SystemTime::now();
        for _ in 0..half_capacity {
            deny_list.add(Bssid([1, 2, 3, 4, 5, 6]), fidl_sme::ConnectResultCode::Failed);
        }

        // Since we do not know time of each entry in the list, check the other values and length
        assert_eq!(half_capacity, deny_list.get_recent(curr_time).len());
        for denial in deny_list.get_recent(curr_time) {
            assert_eq!([1, 2, 3, 4, 5, 6], denial.bssid.0);
            assert_eq!(fidl_sme::ConnectResultCode::Failed, denial.reason);
        }

        // Add one more and check list again
        deny_list.add(Bssid([1, 2, 3, 4, 5, 6]), fidl_sme::ConnectResultCode::Failed);
        assert_eq!(half_capacity + 1, deny_list.get_recent(curr_time).len());
        for denial in deny_list.get_recent(curr_time) {
            assert_eq!([1, 2, 3, 4, 5, 6], denial.bssid.0);
            assert_eq!(fidl_sme::ConnectResultCode::Failed, denial.reason);
        }
    }

    #[test]
    fn test_credential_from_bytes() {
        assert_eq!(Credential::from_bytes(vec![1]), Credential::Password(vec![1]));
        assert_eq!(Credential::from_bytes(vec![2; 63]), Credential::Password(vec![2; 63]));
        assert_eq!(Credential::from_bytes(vec![2; 64]), Credential::Psk(vec![2; 64]));
        assert_eq!(Credential::from_bytes(vec![]), Credential::None);
    }

    #[test]
    fn test_derived_security_type_from_credential() {
        let password = Credential::Password(b"password".to_vec());
        let psk = Credential::Psk(b"psk-type".to_vec());
        let none = Credential::None;

        assert_eq!(SecurityType::Wpa2, password.derived_security_type());
        assert_eq!(SecurityType::Wpa2, psk.derived_security_type());
        assert_eq!(SecurityType::None, none.derived_security_type());
    }
}
