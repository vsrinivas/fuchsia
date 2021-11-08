// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow, fidl_fuchsia_wlan_policy as wlan_policy};

#[derive(Debug, PartialEq)]
pub enum SecurityType {
    None,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
}

impl ::std::convert::From<SecurityType> for wlan_policy::SecurityType {
    fn from(arg: SecurityType) -> Self {
        match arg {
            SecurityType::None => wlan_policy::SecurityType::None,
            SecurityType::Wep => wlan_policy::SecurityType::Wep,
            SecurityType::Wpa => wlan_policy::SecurityType::Wpa,
            SecurityType::Wpa2 => wlan_policy::SecurityType::Wpa2,
            SecurityType::Wpa3 => wlan_policy::SecurityType::Wpa3,
        }
    }
}

impl std::str::FromStr for SecurityType {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "none" => Ok(SecurityType::None),
            "wep" => Ok(SecurityType::Wep),
            "wpa" => Ok(SecurityType::Wpa),
            "wpa2" => Ok(SecurityType::Wpa2),
            "wpa3" => Ok(SecurityType::Wpa3),
            other => Err(anyhow::format_err!("could not parse security type: {}", other)),
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum CredentialType {
    None,
    Psk,
    Password,
}

impl std::str::FromStr for CredentialType {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "none" => Ok(CredentialType::None),
            "psk" => Ok(CredentialType::Psk),
            "password" => Ok(CredentialType::Password),
            other => Err(anyhow::format_err!("could not parse security type: {}", other)),
        }
    }
}

fn validate_credential(
    security_type: &SecurityType,
    credential_type: &CredentialType,
) -> Result<(), anyhow::Error> {
    if *credential_type == CredentialType::None && *security_type != SecurityType::None {
        return Err(anyhow::format_err!(
            "When SecurityType != None, you must provide a CredentialType and Credential"
        ));
    } else if *credential_type != CredentialType::None && *security_type == SecurityType::None {
        return Err(
            anyhow::format_err!("When SecurityType == None, you must use CredentialType::None and may must not provide a Credential")
        );
    }
    Ok(())
}

fn construct_credential(
    credential_type: CredentialType,
    credential: String,
) -> wlan_policy::Credential {
    match credential_type {
        CredentialType::r#None => wlan_policy::Credential::None(wlan_policy::Empty),
        CredentialType::Psk => {
            // The PSK is given in a 64 character hexadecimal string. Config args are safe to
            // unwrap because the tool requires them to be present in the command.
            let psk_arg = credential.as_bytes().to_vec();
            let psk = hex::decode(psk_arg).expect(
                "Error: PSK must be 64 hexadecimal characters.\
                Example: \"123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1234\"",
            );
            wlan_policy::Credential::Psk(psk)
        }
        CredentialType::Password => {
            wlan_policy::Credential::Password(credential.as_bytes().to_vec())
        }
    }
}

pub fn id_from_args(ssid: String, security_type: SecurityType) -> wlan_policy::NetworkIdentifier {
    wlan_policy::NetworkIdentifier {
        ssid: ssid.as_bytes().to_vec(),
        type_: wlan_policy::SecurityType::from(security_type),
    }
}

pub fn config_from_args(
    ssid: String,
    security_type: SecurityType,
    credential_type: CredentialType,
    credential: String,
) -> wlan_policy::NetworkConfig {
    validate_credential(&security_type, &credential_type).unwrap();
    let credential = construct_credential(credential_type, credential);
    let network_id = id_from_args(ssid, security_type);
    wlan_policy::NetworkConfig {
        id: Some(network_id),
        credential: Some(credential),
        ..wlan_policy::NetworkConfig::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    pub struct NetworkConfig {
        ssid: String,
        security_type: SecurityType,
        credential_type: CredentialType,
        credential: String,
    }

    impl From<NetworkConfig> for wlan_policy::NetworkConfig {
        fn from(arg: NetworkConfig) -> Self {
            config_from_args(arg.ssid, arg.security_type, arg.credential_type, arg.credential)
        }
    }

    /// Tests that a WEP network config will be correctly translated for save and remove network.
    #[test]
    fn test_construct_config_wep() {
        test_construct_config_security(wlan_policy::SecurityType::Wep, SecurityType::Wep);
    }

    /// Tests that a WPA network config will be correctly translated for save and remove network.
    #[test]
    fn test_construct_config_wpa() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa, SecurityType::Wpa);
    }

    /// Tests that a WPA2 network config will be correctly translated for save and remove network.
    #[test]
    fn test_construct_config_wpa2() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa2, SecurityType::Wpa2);
    }

    /// Tests that a WPA3 network config will be correctly translated for save and remove network.
    #[test]
    fn test_construct_config_wpa3() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa3, SecurityType::Wpa3);
    }

    /// Tests that a config for an open network will be correctly translated to FIDL values for
    /// save and remove network.
    #[test]
    fn test_construct_config_open() {
        let open_config = NetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityType::None,
            credential_type: CredentialType::None,
            credential: "".to_string(),
        };
        let expected_cfg = wlan_policy::NetworkConfig {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: "some_ssid".as_bytes().to_vec(),
                type_: wlan_policy::SecurityType::None,
            }),
            credential: Some(wlan_policy::Credential::None(wlan_policy::Empty {})),
            ..wlan_policy::NetworkConfig::EMPTY
        };
        let result_cfg = wlan_policy::NetworkConfig::from(open_config);
        assert_eq!(expected_cfg, result_cfg);
    }

    /// Tests that a config for an open network with a password will fail gracefully.
    #[test]
    #[should_panic]
    fn test_construct_config_open_with_password() {
        let malformed_open_config = NetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityType::None,
            credential_type: CredentialType::Password,
            credential: "".to_string(),
        };
        let _errmsg = wlan_policy::NetworkConfig::from(malformed_open_config);
    }

    /// Tests that a config for a protected network without a password will fail gracefully.
    #[test]
    #[should_panic]
    fn test_construct_config_protected_without_password() {
        let malformed_wpa2_config = NetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityType::Wpa2,
            credential_type: CredentialType::None,
            credential: "".to_string(),
        };
        let _ = wlan_policy::NetworkConfig::from(malformed_wpa2_config);
    }

    /// Test that a config with a PSK will be translated correctly, including a transfer from a
    /// hex string to bytes.
    #[test]
    fn test_construct_config_psk() {
        // Test PSK separately since it has a unique credential
        const ASCII_ZERO: u8 = 49;
        let psk =
            String::from_utf8([ASCII_ZERO; 64].to_vec()).expect("Failed to create PSK test value");
        let wpa_config = NetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityType::Wpa2,
            credential_type: CredentialType::Psk,
            credential: psk,
        };
        let expected_cfg = wlan_policy::NetworkConfig {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: "some_ssid".as_bytes().to_vec(),
                type_: wlan_policy::SecurityType::Wpa2,
            }),
            credential: Some(wlan_policy::Credential::Psk([17; 32].to_vec())),
            ..wlan_policy::NetworkConfig::EMPTY
        };
        let result_cfg = wlan_policy::NetworkConfig::from(wpa_config);
        assert_eq!(expected_cfg, result_cfg);
    }

    /// Test that the given variant of security type with a password works when constructing
    /// network configs as used by save and remove network.
    fn test_construct_config_security(
        fidl_type: wlan_policy::SecurityType,
        tool_type: SecurityType,
    ) {
        let wpa_config = NetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: tool_type,
            credential_type: CredentialType::Password,
            credential: "some_password_here".to_string(),
        };
        let expected_cfg = wlan_policy::NetworkConfig {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: "some_ssid".as_bytes().to_vec(),
                type_: fidl_type,
            }),
            credential: Some(wlan_policy::Credential::Password(
                "some_password_here".as_bytes().to_vec(),
            )),
            ..wlan_policy::NetworkConfig::EMPTY
        };
        let result_cfg = wlan_policy::NetworkConfig::from(wpa_config);
        assert_eq!(expected_cfg, result_cfg);
    }
}
