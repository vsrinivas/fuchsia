// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[allow(deprecated)] // Necessary for AsciiExt usage from clap args_enum macro
use anyhow::{format_err, Error};
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

/// Remove args are similar to SaveNetworkArgs but with optional security type and credential
/// because it can match to saved networks with just SSID.
/// Examples of valid arguments:
///     --ssid MyNetwork
///     --ssid MyNetwork --security-type wpa2
///     --ssid MyNetwork --credential MyPassword (defaults to credential-type: password)
///     --ssid MyNetwork --credential-type password --credential MyPassword
/// Example of invalid arguments:
///     --ssid MyNetwork --security-type none --credential MyPassword
///     --ssid MyNetwork --credential-type password
#[derive(StructOpt, Clone, Debug)]
pub struct RemoveArgs {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: Option<SecurityTypeArg>,
    #[structopt(
        long = "credential-type",
        raw(possible_values = "&CredentialTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub credential_type: Option<CredentialTypeArg>,
    #[structopt(long)]
    pub credential: Option<String>,
}

impl RemoveArgs {
    pub fn parse_security(&self) -> Option<wlan_policy::SecurityType> {
        self.security_type.map(|s| s.into())
    }

    /// Determine the credential to use based on security_type, credential_type, and credential. If
    /// a value is provided without a type, Password will be used by default for open networks.
    ///
    /// This includes a bunch of if statements for edge cases such as a credential being provided
    /// as "" or a credential type that conflicts with the credential value. But it does not check
    /// all input errors such as invalid PSK length or all conflicting security credential pairs.
    pub fn try_parse_credential(&self) -> Result<Option<wlan_policy::Credential>, Error> {
        let credential = if let Some(cred_val) = &self.credential {
            match self.credential_type {
                Some(CredentialTypeArg::Password) => {
                    Some(wlan_policy::Credential::Password(cred_val.as_bytes().to_vec()))
                }
                Some(CredentialTypeArg::Psk) => {
                    // The PSK is given in a 64 character hexadecimal string. Config args are safe to
                    // unwrap because the tool requires them to be present in the command.
                    let psk_arg = cred_val.as_bytes().to_vec();
                    let psk = hex::decode(psk_arg).expect(
                        "Error: PSK must be 64 hexadecimal characters.\
                        Example: \"123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1234\"",
                    );
                    Some(wlan_policy::Credential::Psk(psk))
                }
                Some(CredentialTypeArg::None) => {
                    // Credential arg shouldn't be provided if the credential type is None; if it
                    // is provided return an error rather than throwing away the input.
                    if cred_val.is_empty() {
                        Some(wlan_policy::Credential::None(wlan_policy::Empty))
                    } else {
                        return Err(format_err!(
                            "A credential value was provided with a credential \
                            type of None. No value be provided if there is no credential, or the \
                            credential type should be PSK or Password."
                        ));
                    }
                }
                None => {
                    // If credential value is provided but its type isn't, default to password.
                    // Except for the edge case of an empty string - assume the user meant
                    // Credential None since passwords can't be empty.
                    if cred_val.is_empty() {
                        Some(wlan_policy::Credential::None(wlan_policy::Empty))
                    } else {
                        Some(wlan_policy::Credential::Password(cred_val.as_bytes().to_vec()))
                    }
                }
            }
        } else {
            if let Some(credential_type) = self.credential_type {
                if credential_type == CredentialTypeArg::None {
                    Some(wlan_policy::Credential::None(wlan_policy::Empty))
                } else {
                    return Err(format_err!(
                        "A credential type was provided with no value. Please \
                        provide the credential to be used"
                    ));
                }
            } else {
                // This is the case where no credential type or value was provided. There is no
                // need to default to Credential::None for open networks since that is the only
                // type of credential that could be saved for them.
                None
            }
        };
        Ok(credential)
    }
}

#[derive(StructOpt, Clone, Debug)]
pub struct SaveNetworkArgs {
    #[structopt(long, required = true)]
    pub ssid: String,
    #[structopt(
        long = "security-type",
        raw(possible_values = "&SecurityTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub security_type: SecurityTypeArg,
    #[structopt(
        long = "credential-type",
        raw(possible_values = "&CredentialTypeArg::variants()"),
        raw(case_insensitive = "true")
    )]
    pub credential_type: Option<CredentialTypeArg>,
    #[structopt(long)]
    pub credential: String,
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
    RemoveNetwork(RemoveArgs),
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
    use {super::*, test_case::test_case};

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

    /// Test cases where the credential and type are not provided and the parsed credential should
    /// be none.
    #[test_case(Some(SecurityTypeArg::None))]
    #[test_case(None)]
    fn test_try_parse_credentialcredential_not_provided(security_type: Option<SecurityTypeArg>) {
        let args = RemoveArgs {
            ssid: "some_ssid".to_string(),
            security_type,
            credential_type: None,
            credential: None,
        };
        let expected_credential = None;
        let result = args.try_parse_credential().expect("error occurred parsing credential");
        assert_eq!(result, expected_credential);
    }

    /// Test that if the credential argument is provided as an empty string, and security type
    /// is none, the credential should be Credential::None
    #[fuchsia::test]
    fn test_try_parse_credential_open_network_credential_empty_string() {
        let args = RemoveArgs {
            ssid: "some_ssid".to_string(),
            security_type: Some(SecurityTypeArg::None),
            credential_type: None,
            credential: Some("".to_string()),
        };
        let expected_credential = Some(wlan_policy::Credential::None(wlan_policy::Empty));
        let result = args.try_parse_credential().expect("error occurred parsing credential");
        assert_eq!(result, expected_credential);
    }

    /// Test that if a password is provided without a type, the credential type is password by
    /// default. The security type should not have a default value.
    #[fuchsia::test]
    fn test_try_parse_credential_with_no_type() {
        let password = "somepassword";
        let args = RemoveArgs {
            ssid: "some_ssid".to_string(),
            security_type: None,
            credential_type: None,
            credential: Some(password.to_string()),
        };
        let expected_credential =
            Some(wlan_policy::Credential::Password(password.as_bytes().to_vec()));
        let result = args.try_parse_credential().expect("error occurred parsing credential");
        assert_eq!(result, expected_credential);
        assert_eq!(args.parse_security(), None);
    }

    /// Test that if the credential type and value are provided, they are used without error. It
    /// does not matter what the security type is.
    #[fuchsia::test]
    fn test_try_parse_credential_exact_args_provided() {
        let psk = "123456789ABCDEF123456789ABCDEF123456789ABCDEF123456789ABCDEF1234";
        let args = RemoveArgs {
            ssid: "some_ssid".to_string(),
            security_type: Some(SecurityTypeArg::None),
            credential_type: Some(CredentialTypeArg::Psk),
            credential: Some(psk.to_string()),
        };
        let expected_credential = Some(wlan_policy::Credential::Psk(hex::decode(psk).unwrap()));
        let result = args.try_parse_credential().expect("error occurred parsing credential");
        assert_eq!(result, expected_credential);
    }

    /// Test that an error occurs when the credential type is provided without a value and the
    /// type is not None.
    #[fuchsia::test]
    fn test_try_parse_credential_password_with_no_value_is_err() {
        let args = RemoveArgs {
            ssid: "some_ssid".to_string(),
            security_type: Some(SecurityTypeArg::Wpa2),
            credential_type: Some(CredentialTypeArg::Password),
            credential: None,
        };
        args.try_parse_credential().expect_err("an error should occur parsing this credential");
    }
}
