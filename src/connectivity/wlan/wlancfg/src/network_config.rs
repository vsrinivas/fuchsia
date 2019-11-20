// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]
use {
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_sme as fidl_sme,
    std::{
        collections::VecDeque,
        thread,
        time::{Duration, SystemTime},
    },
    wlan_common::mac::Bssid,
};

/// (persist) Used to determine what version of network configs are stored on disk so we know
/// how to interpret the data.
const CURRENT_VERSION: u16 = 1;
/// The maximum number of denied connection reasons we will store for one network at a time.
/// For now this number is chosen arbitrarily.
const NUM_DENY_REASONS: usize = 10;

/// The network identifier is the SSID and security policy of the network, and it is used to
/// distinguish networks. It mirrors the NetworkIdentifier in fidl_fuchsia_wlan_policy.
pub type NetworkIdentifier = (Vec<u8>, fidl_policy::SecurityType);
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

    pub fn add(&mut self, bssid: Bssid, reason: fidl_sme::ConnectResultCode) {
        if self.0.len() == self.0.capacity() {
            self.0.pop_front();
        }
        self.0.push_back(NetworkDenial { bssid, time: SystemTime::now(), reason });
    }

    /// return a list of the denials that happened at or after given system time
    pub fn get_recent(&self, earliest_time: SystemTime) -> Vec<NetworkDenial> {
        self.0.iter().skip_while(|denial| denial.time < earliest_time).cloned().collect()
    }
}

/// Saved data for networks, to remember how to connect to a network and determine if we should.
#[derive(Debug, PartialEq)]
pub struct NetworkConfig {
    /// (persist) SSID and security type to identify a network.
    pub ssid: Vec<u8>,
    pub security_type: fidl_policy::SecurityType,
    /// (persist) Credential to connect to a protected network or None if the network is open.
    pub credential: fidl_policy::Credential,
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
        credential: fidl_policy::Credential,
        has_ever_connected: bool,
        seen_in_passive_scan_results: bool,
    ) -> Result<Self, SaveError> {
        if let Some(error) = check_config_errors(&id.0, &id.1, &credential) {
            return Err(error);
        }

        Ok(Self {
            ssid: id.0,
            security_type: id.1,
            credential,
            has_ever_connected,
            seen_in_passive_scan_results,
            perf_stats: PerformanceStats { deny_list: NetworkDenialList::new() },
        })
    }
}

impl Clone for NetworkConfig {
    fn clone(&self) -> Self {
        NetworkConfig {
            ssid: self.ssid.clone(),
            security_type: self.security_type,
            credential: clone_credential(&self.credential),
            has_ever_connected: self.has_ever_connected,
            seen_in_passive_scan_results: self.seen_in_passive_scan_results,
            perf_stats: self.perf_stats.clone(),
        }
    }
}

/// Returns an error if the input network values are not valid or none if the values are valid.
/// For example it is an error if the network is Open (no password) but a password is supplied.
/// TODO(nmccracken) - Specific errors need to be added to the API and returned here
fn check_config_errors(
    ssid: &Vec<u8>,
    security_type: &fidl_policy::SecurityType,
    credential: &fidl_policy::Credential,
) -> Option<SaveError> {
    use fidl_policy::SecurityType::{None as SecurityNone, Wpa, Wpa2, Wpa3};

    // Verify SSID has at most 32 characters
    if ssid.len() > 32 {
        return Some(SaveError::GeneralError);
    }
    // Verify that credentials match security type
    match security_type {
        SecurityNone => {
            if let fidl_policy::Credential::Psk(_) | fidl_policy::Credential::Password(_) =
                credential
            {
                return Some(SaveError::GeneralError);
            }
        }
        Wpa | Wpa2 | Wpa3 => match &credential {
            fidl_policy::Credential::Password(pwd) => {
                if pwd.clone().len() < 8 || pwd.clone().len() > 63 {
                    return Some(SaveError::GeneralError);
                }
            }
            fidl_policy::Credential::Psk(psk) => {
                if psk.clone().len() != 64 {
                    return Some(SaveError::GeneralError);
                }
            }
            _ => {
                return Some(SaveError::GeneralError);
            }
        },
        _ => {}
    }
    None
}

pub fn clone_credential(credential: &fidl_policy::Credential) -> fidl_policy::Credential {
    match credential {
        fidl_policy::Credential::Password(pwd) => fidl_policy::Credential::Password(pwd.clone()),
        fidl_policy::Credential::Psk(psk) => fidl_policy::Credential::Psk(psk.clone()),
        _ => fidl_policy::Credential::None(fidl_policy::Empty {}),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_wlan_policy as fidl_policy, wlan_common::assert_variant};

    #[test]
    fn new_network_config_none_credential() {
        use fidl_policy::SecurityType::None as SecurityNone;
        let credential = fidl_policy::Credential::None(fidl_policy::Empty);
        let network_config =
            NetworkConfig::new((b"foo".to_vec(), SecurityNone), credential, false, false)
                .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, SecurityNone);
        assert_eq!(network_config.credential, fidl_policy::Credential::None(fidl_policy::Empty));
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_password_credential() {
        use fidl_policy::SecurityType::Wpa2;
        let credential = fidl_policy::Credential::Password(b"foo-password".to_vec());

        let network_config = NetworkConfig::new((b"foo".to_vec(), Wpa2), credential, false, false)
            .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, Wpa2);
        assert_eq!(
            network_config.credential,
            fidl_policy::Credential::Password(b"foo-password".to_vec())
        );
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_psk_credential() {
        use fidl_policy::SecurityType::Wpa2;
        let credential = fidl_policy::Credential::Psk([1; 64].to_vec());

        let network_config = NetworkConfig::new((b"foo".to_vec(), Wpa2), credential, false, false)
            .expect("Error creating network config for foo");

        assert_eq!(network_config.ssid, b"foo".to_vec());
        assert_eq!(network_config.security_type, Wpa2);
        assert_eq!(network_config.credential, fidl_policy::Credential::Psk([1; 64].to_vec()));
        assert_eq!(network_config.has_ever_connected, false);
        assert!(network_config.perf_stats.deny_list.0.is_empty());
    }

    #[test]
    fn new_network_config_invalid_ssid() {
        use fidl_policy::SecurityType::Wpa2;
        let credential = fidl_policy::Credential::None(fidl_policy::Empty);

        let network_config = NetworkConfig::new(([1; 33].to_vec(), Wpa2), credential, false, false);

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn new_network_config_invalid_password() {
        use fidl_policy::SecurityType::Wpa;
        let credential = fidl_policy::Credential::Password([1; 64].to_vec());

        let network_config = NetworkConfig::new((b"foo".to_vec(), Wpa), credential, false, false);

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn new_network_config_invalid_psk() {
        use fidl_policy::SecurityType::Wpa2;
        let credential = fidl_policy::Credential::Psk(b"bar".to_vec());

        let network_config = NetworkConfig::new((b"foo".to_vec(), Wpa2), credential, false, false);

        assert_variant!(network_config, Result::Err(err) =>
            {assert_eq!(err, SaveError::GeneralError);}
        );
    }

    #[test]
    fn deny_list_add_and_get() {
        let mut deny_list = NetworkDenialList::new();

        // get time before adding so we can get back everything we added
        let curr_time = SystemTime::now();
        assert!(deny_list.get_recent(curr_time).is_empty());
        deny_list.add(Bssid([1, 2, 3, 4, 5, 6]), fidl_sme::ConnectResultCode::Failed);

        let result_list = deny_list.get_recent(curr_time);
        assert_eq!(1, result_list.len());
        assert_eq!([1, 2, 3, 4, 5, 6], result_list[0].bssid.0);
        assert_eq!(fidl_sme::ConnectResultCode::Failed, result_list[0].reason);
        // should not get any results if we request for more recent denials more recent than added
        assert!(deny_list.get_recent(SystemTime::now() + Duration::new(0, 1)).is_empty());
    }

    #[test]
    fn deny_list_add_when_full() {
        let mut deny_list = NetworkDenialList::new();

        let curr_time = SystemTime::now();
        assert!(deny_list.get_recent(curr_time).is_empty());
        let deny_list_capacity = deny_list.0.capacity();
        assert!(deny_list_capacity >= NUM_DENY_REASONS);
        for i in (0..deny_list_capacity + 2) {
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
        for i in (0..half_capacity) {
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
}
