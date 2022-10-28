// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro
use clap::arg_enum;
use eui48::MacAddress;
use fidl_fuchsia_wlan_common as wlan_common;
use fidl_fuchsia_wlan_policy as wlan_policy;
use structopt::StructOpt;

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum RoleArg {
        Client,
        Ap
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum ScanTypeArg {
        Active,
        Passive,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum SecurityTypeArg {
        None,
        Wep,
        Wpa,
        Wpa2,
        Wpa3,
    }
}

arg_enum! {
    #[derive(PartialEq, Copy, Clone, Debug)]
    pub enum CredentialTypeArg {
        None,
        Psk,
        Password,
    }
}

impl From<RoleArg> for wlan_common::WlanMacRole {
    fn from(arg: RoleArg) -> Self {
        match arg {
            RoleArg::Client => wlan_common::WlanMacRole::Client,
            RoleArg::Ap => wlan_common::WlanMacRole::Ap,
        }
    }
}

impl From<ScanTypeArg> for wlan_common::ScanType {
    fn from(arg: ScanTypeArg) -> Self {
        match arg {
            ScanTypeArg::Active => wlan_common::ScanType::Active,
            ScanTypeArg::Passive => wlan_common::ScanType::Passive,
        }
    }
}

impl From<SecurityTypeArg> for wlan_policy::SecurityType {
    fn from(arg: SecurityTypeArg) -> Self {
        match arg {
            SecurityTypeArg::r#None => wlan_policy::SecurityType::None,
            SecurityTypeArg::Wep => wlan_policy::SecurityType::Wep,
            SecurityTypeArg::Wpa => wlan_policy::SecurityType::Wpa,
            SecurityTypeArg::Wpa2 => wlan_policy::SecurityType::Wpa2,
            SecurityTypeArg::Wpa3 => wlan_policy::SecurityType::Wpa3,
        }
    }
}

impl From<PolicyNetworkConfig> for wlan_policy::NetworkConfig {
    fn from(arg: PolicyNetworkConfig) -> Self {
        let security_type = wlan_policy::SecurityType::from(arg.security_type);
        if (arg.credential_type == CredentialTypeArg::r#None
            && security_type != wlan_policy::SecurityType::None)
            || (arg.credential_type != CredentialTypeArg::r#None
                && security_type == wlan_policy::SecurityType::None)
        {
            panic!(
                "Invalid credential type {:?} for security type {:?}",
                arg.credential_type, security_type
            );
        }

        let credential = match arg.credential_type {
            CredentialTypeArg::r#None => wlan_policy::Credential::None(wlan_policy::Empty),
            CredentialTypeArg::Psk => {
                // The PSK is given in a 64 character hexadecimal string. Config args are safe to
                // unwrap because the tool requires them to be present in the command.
                let psk_arg = arg.credential.unwrap().as_bytes().to_vec();
                let psk = hex::decode(psk_arg).expect(
                    "Error: PSK must be 64 hexadecimal characters.\
                    Example: \"123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1234\"",
                );
                wlan_policy::Credential::Psk(psk)
            }
            CredentialTypeArg::Password => {
                wlan_policy::Credential::Password(arg.credential.unwrap().as_bytes().to_vec())
            }
        };

        let network_id = wlan_policy::NetworkIdentifier {
            ssid: arg.ssid.as_bytes().to_vec(),
            type_: security_type,
        };
        wlan_policy::NetworkConfig {
            id: Some(network_id),
            credential: Some(credential),
            ..wlan_policy::NetworkConfig::EMPTY
        }
    }
}

#[derive(StructOpt, Clone, Debug)]
pub struct PolicyNetworkConfig {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        default_value = "none",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: SecurityTypeArg,
    #[structopt(
        long = "credential-type",
        default_value = "none",
        raw(possible_values = "&CredentialTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub credential_type: CredentialTypeArg,
    #[structopt(long)]
    pub credential: Option<String>,
}

#[derive(StructOpt, Clone, Debug)]
pub struct ConnectArgs {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: Option<SecurityTypeArg>,
}

#[derive(StructOpt, Clone, Debug)]
pub enum PolicyClientCmd {
    #[structopt(name = "connect")]
    Connect(ConnectArgs),
    #[structopt(name = "list-saved-networks")]
    GetSavedNetworks,
    #[structopt(name = "listen")]
    Listen,
    #[structopt(name = "remove-network")]
    RemoveNetwork(PolicyNetworkConfig),
    #[structopt(name = "save-network")]
    SaveNetwork(PolicyNetworkConfig),
    #[structopt(name = "scan")]
    ScanForNetworks,
    #[structopt(name = "start-client-connections")]
    StartClientConnections,
    #[structopt(name = "stop-client-connections")]
    StopClientConnections,
    #[structopt(name = "dump-config")]
    DumpConfig,
    #[structopt(name = "restore-config")]
    RestoreConfig { serialized_config: String },
}

#[derive(StructOpt, Clone, Debug)]
pub enum PolicyAccessPointCmd {
    // TODO(sakuma): Allow users to specify connectivity mode and operating band.
    #[structopt(name = "start")]
    Start(PolicyNetworkConfig),
    #[structopt(name = "stop")]
    Stop(PolicyNetworkConfig),
    #[structopt(name = "stop-all")]
    StopAllAccessPoints,
    #[structopt(name = "listen")]
    Listen,
}

#[derive(StructOpt, Clone, Debug)]
pub enum DeprecatedConfiguratorCmd {
    #[structopt(name = "suggest-mac")]
    SuggestAccessPointMacAddress {
        #[structopt(raw(required = "true"))]
        mac: MacAddress,
    },
}

#[derive(StructOpt, Clone, Debug)]
pub enum Opt {
    #[structopt(name = "client")]
    Client(PolicyClientCmd),
    #[structopt(name = "ap")]
    AccessPoint(PolicyAccessPointCmd),
    #[structopt(name = "deprecated")]
    Deprecated(DeprecatedConfiguratorCmd),
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests that a WEP network config will be correctly translated for save and remove network.
    #[fuchsia::test]
    fn test_construct_config_wep() {
        test_construct_config_security(wlan_policy::SecurityType::Wep, SecurityTypeArg::Wep);
    }

    /// Tests that a WPA network config will be correctly translated for save and remove network.
    #[fuchsia::test]
    fn test_construct_config_wpa() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa, SecurityTypeArg::Wpa);
    }

    /// Tests that a WPA2 network config will be correctly translated for save and remove network.
    #[fuchsia::test]
    fn test_construct_config_wpa2() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa2, SecurityTypeArg::Wpa2);
    }

    /// Tests that a WPA3 network config will be correctly translated for save and remove network.
    #[fuchsia::test]
    fn test_construct_config_wpa3() {
        test_construct_config_security(wlan_policy::SecurityType::Wpa3, SecurityTypeArg::Wpa3);
    }

    /// Tests that a config for an open network will be correctly translated to FIDL values for
    /// save and remove network.
    #[fuchsia::test]
    fn test_construct_config_open() {
        let open_config = PolicyNetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityTypeArg::None,
            credential_type: CredentialTypeArg::None,
            credential: Some("".to_string()),
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
    #[fuchsia::test]
    #[should_panic]
    fn test_construct_config_open_with_password() {
        let malformed_open_config = PolicyNetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityTypeArg::None,
            credential_type: CredentialTypeArg::Password,
            credential: Some("".to_string()),
        };
        let _errmsg = wlan_policy::NetworkConfig::from(malformed_open_config);
    }

    /// Tests that a config for a protected network without a password will fail gracefully.
    #[fuchsia::test]
    #[should_panic]
    fn test_construct_config_protected_without_password() {
        let malformed_wpa2_config = PolicyNetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityTypeArg::Wpa2,
            credential_type: CredentialTypeArg::None,
            credential: Some("".to_string()),
        };
        let _ = wlan_policy::NetworkConfig::from(malformed_wpa2_config);
    }

    /// Test that a config with a PSK will be translated correctly, including a transfer from a
    /// hex string to bytes.
    #[fuchsia::test]
    fn test_construct_config_psk() {
        // Test PSK separately since it has a unique credential
        const ASCII_ZERO: u8 = 49;
        let psk =
            String::from_utf8([ASCII_ZERO; 64].to_vec()).expect("Failed to create PSK test value");
        let wpa_config = PolicyNetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: SecurityTypeArg::Wpa2,
            credential_type: CredentialTypeArg::Psk,
            credential: Some(psk),
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
        tool_type: SecurityTypeArg,
    ) {
        let wpa_config = PolicyNetworkConfig {
            ssid: "some_ssid".to_string(),
            security_type: tool_type,
            credential_type: CredentialTypeArg::Password,
            credential: Some("some_password_here".to_string()),
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
