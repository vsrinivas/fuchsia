// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::network_config::{Credential, NetworkConfig, NetworkIdentifier, SecurityType},
    wlan_stash::policy as policy_stash,
};

impl From<policy_stash::NetworkIdentifier> for NetworkIdentifier {
    fn from(item: policy_stash::NetworkIdentifier) -> Self {
        Self { ssid: item.ssid, security_type: item.security_type.into() }
    }
}

impl From<NetworkIdentifier> for policy_stash::NetworkIdentifier {
    fn from(item: NetworkIdentifier) -> Self {
        Self { ssid: item.ssid, security_type: item.security_type.into() }
    }
}

impl From<policy_stash::SecurityType> for SecurityType {
    fn from(item: policy_stash::SecurityType) -> Self {
        match item {
            policy_stash::SecurityType::None => SecurityType::None,
            policy_stash::SecurityType::Wep => SecurityType::Wep,
            policy_stash::SecurityType::Wpa => SecurityType::Wpa,
            policy_stash::SecurityType::Wpa2 => SecurityType::Wpa2,
            policy_stash::SecurityType::Wpa3 => SecurityType::Wpa3,
        }
    }
}

impl From<SecurityType> for policy_stash::SecurityType {
    fn from(item: SecurityType) -> Self {
        match item {
            SecurityType::None => policy_stash::SecurityType::None,
            SecurityType::Wep => policy_stash::SecurityType::Wep,
            SecurityType::Wpa => policy_stash::SecurityType::Wpa,
            SecurityType::Wpa2 => policy_stash::SecurityType::Wpa2,
            SecurityType::Wpa3 => policy_stash::SecurityType::Wpa3,
        }
    }
}

impl From<policy_stash::Credential> for Credential {
    fn from(item: policy_stash::Credential) -> Self {
        match item {
            policy_stash::Credential::None => Credential::None,
            policy_stash::Credential::Password(pass) => Credential::Password(pass),
            policy_stash::Credential::Psk(psk) => Credential::Psk(psk),
        }
    }
}

impl From<Credential> for policy_stash::Credential {
    fn from(item: Credential) -> Self {
        match item {
            Credential::None => policy_stash::Credential::None,
            Credential::Password(pass) => policy_stash::Credential::Password(pass),
            Credential::Psk(psk) => policy_stash::Credential::Psk(psk),
        }
    }
}

impl From<NetworkConfig> for policy_stash::PersistentData {
    fn from(item: NetworkConfig) -> Self {
        Self { credential: item.credential.into(), has_ever_connected: item.has_ever_connected }
    }
}

pub fn network_config_vec_to_persistent_data(
    network_config: &Vec<NetworkConfig>,
) -> Vec<policy_stash::PersistentData> {
    network_config.iter().map(|c| c.clone().into()).collect()
}

#[cfg(test)]
mod tests {
    use {
        super::{super::network_config::PerformanceStats, *},
        wlan_stash::policy as pstash,
    };

    #[test]
    fn network_identifier_to_stash_from_policy() {
        assert_eq!(
            pstash::NetworkIdentifier::from(NetworkIdentifier {
                ssid: b"ssid".to_vec(),
                security_type: SecurityType::Wep,
            }),
            pstash::NetworkIdentifier {
                ssid: b"ssid".to_vec(),
                security_type: pstash::SecurityType::Wep,
            }
        );
    }

    #[test]
    fn network_identifier_to_policy_from_stash() {
        assert_eq!(
            NetworkIdentifier::from(pstash::NetworkIdentifier {
                ssid: b"ssid".to_vec(),
                security_type: pstash::SecurityType::Wep,
            }),
            NetworkIdentifier { ssid: b"ssid".to_vec(), security_type: SecurityType::Wep }
        );
    }

    #[test]
    fn security_type_to_stash_from_policy() {
        assert_eq!(pstash::SecurityType::from(SecurityType::None), pstash::SecurityType::None);
        assert_eq!(pstash::SecurityType::from(SecurityType::Wep), pstash::SecurityType::Wep);
        assert_eq!(pstash::SecurityType::from(SecurityType::Wpa), pstash::SecurityType::Wpa);
        assert_eq!(pstash::SecurityType::from(SecurityType::Wpa2), pstash::SecurityType::Wpa2);
        assert_eq!(pstash::SecurityType::from(SecurityType::Wpa3), pstash::SecurityType::Wpa3);
    }

    #[test]
    fn security_type_to_policy_from_stash() {
        assert_eq!(SecurityType::from(pstash::SecurityType::None), SecurityType::None);
        assert_eq!(SecurityType::from(pstash::SecurityType::Wep), SecurityType::Wep);
        assert_eq!(SecurityType::from(pstash::SecurityType::Wpa), SecurityType::Wpa);
        assert_eq!(SecurityType::from(pstash::SecurityType::Wpa2), SecurityType::Wpa2);
        assert_eq!(SecurityType::from(pstash::SecurityType::Wpa3), SecurityType::Wpa3);
    }

    #[test]
    fn credential_to_stash_from_policy() {
        assert_eq!(pstash::Credential::from(Credential::None), pstash::Credential::None);
        assert_eq!(
            pstash::Credential::from(Credential::Password(b"foo_pass123".to_vec())),
            pstash::Credential::Password(b"foo_pass123".to_vec())
        );
        assert_eq!(
            pstash::Credential::from(Credential::Psk(b"foo_psk123".to_vec())),
            pstash::Credential::Psk(b"foo_psk123".to_vec())
        );
    }

    #[test]
    fn credential_to_policy_from_stash() {
        assert_eq!(Credential::from(pstash::Credential::None), Credential::None);
        assert_eq!(
            Credential::from(pstash::Credential::Password(b"foo_pass123".to_vec())),
            Credential::Password(b"foo_pass123".to_vec())
        );
        assert_eq!(
            Credential::from(pstash::Credential::Password(b"foo_psk123".to_vec())),
            Credential::Password(b"foo_psk123".to_vec())
        );
    }

    #[test]
    fn persistent_data_from_network_config() {
        // has_ever_connected: false
        assert_eq!(
            pstash::PersistentData::from(NetworkConfig {
                ssid: b"ssid".to_vec(),
                security_type: SecurityType::Wpa3,
                credential: Credential::Password(b"foo_pass".to_vec()),
                has_ever_connected: false,
                seen_in_passive_scan_results: true,
                perf_stats: PerformanceStats::new(),
            }),
            pstash::PersistentData {
                credential: pstash::Credential::Password(b"foo_pass".to_vec()),
                has_ever_connected: false
            }
        );

        // has_ever_connected: true
        assert_eq!(
            pstash::PersistentData::from(NetworkConfig {
                ssid: b"ssid".to_vec(),
                security_type: SecurityType::None,
                credential: Credential::None,
                has_ever_connected: true,
                seen_in_passive_scan_results: false,
                perf_stats: PerformanceStats::new(),
            }),
            pstash::PersistentData {
                credential: pstash::Credential::None,
                has_ever_connected: true
            }
        );
    }
}
