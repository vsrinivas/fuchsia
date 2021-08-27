// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    core::convert::{TryFrom, TryInto},
    fidl_fuchsia_wlan_policy as wlan_policy,
    percent_encoding::{percent_decode_str, utf8_percent_encode, AsciiSet, CONTROLS},
    serde::{Deserialize, Serialize},
    serde_json,
};

#[derive(Serialize, Deserialize)]
struct WlanConfigDump {
    version: u32,
    data: Vec<SimplifiedNetworkConfig>,
}
#[derive(Serialize, Deserialize, Clone)]
struct SimplifiedNetworkConfig {
    ssid: Vec<u8>,
    credential: Credential,
    security_type: SecurityType,
}

#[derive(Serialize, Deserialize, Clone)]
enum Credential {
    None,
    Password(Vec<u8>),
    Psk(Vec<u8>),
}

#[derive(Serialize, Deserialize, Clone)]
enum SecurityType {
    None = 1,
    Wep = 2,
    Wpa = 3,
    Wpa2 = 4,
    Wpa3 = 5,
}

impl From<wlan_policy::SecurityType> for SecurityType {
    fn from(security: wlan_policy::SecurityType) -> Self {
        match security {
            wlan_policy::SecurityType::None => SecurityType::None,
            wlan_policy::SecurityType::Wep => SecurityType::Wep,
            wlan_policy::SecurityType::Wpa => SecurityType::Wpa,
            wlan_policy::SecurityType::Wpa2 => SecurityType::Wpa2,
            wlan_policy::SecurityType::Wpa3 => SecurityType::Wpa3,
        }
    }
}

impl From<SecurityType> for wlan_policy::SecurityType {
    fn from(security: SecurityType) -> Self {
        match security {
            SecurityType::None => wlan_policy::SecurityType::None,
            SecurityType::Wep => wlan_policy::SecurityType::Wep,
            SecurityType::Wpa => wlan_policy::SecurityType::Wpa,
            SecurityType::Wpa2 => wlan_policy::SecurityType::Wpa2,
            SecurityType::Wpa3 => wlan_policy::SecurityType::Wpa3,
        }
    }
}

impl TryFrom<wlan_policy::Credential> for Credential {
    type Error = &'static str;

    fn try_from(security: wlan_policy::Credential) -> Result<Self, Self::Error> {
        match security {
            wlan_policy::Credential::None(_) => Ok(Credential::None),
            wlan_policy::Credential::Password(pass) => Ok(Credential::Password(pass)),
            wlan_policy::Credential::Psk(psk) => Ok(Credential::Psk(psk)),
            wlan_policy::CredentialUnknown!() => Err("Unrecognized credential"),
        }
    }
}

impl From<Credential> for wlan_policy::Credential {
    fn from(security: Credential) -> Self {
        match security {
            Credential::None => wlan_policy::Credential::None(wlan_policy::Empty),
            Credential::Password(pass) => wlan_policy::Credential::Password(pass),
            Credential::Psk(psk) => wlan_policy::Credential::Psk(psk),
        }
    }
}

impl TryFrom<wlan_policy::NetworkConfig> for SimplifiedNetworkConfig {
    type Error = &'static str;

    fn try_from(config: wlan_policy::NetworkConfig) -> Result<Self, Self::Error> {
        let id = match config.id {
            Some(id) => id,
            None => return Err("Network has no identifier"),
        };

        let credential = match config.credential {
            Some(credential) => credential,
            None => return Err("Network has no credential"),
        };

        let credential = credential.try_into()?;

        Ok(SimplifiedNetworkConfig {
            ssid: id.ssid,
            credential: credential,
            security_type: id.type_.into(),
        })
    }
}

impl From<SimplifiedNetworkConfig> for wlan_policy::NetworkConfig {
    fn from(simplified_config: SimplifiedNetworkConfig) -> Self {
        Self {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: simplified_config.ssid,
                type_: simplified_config.security_type.into(),
            }),
            credential: Some(simplified_config.credential.into()),
            ..Self::EMPTY
        }
    }
}

/// Serializes a vector of network configurations for storing
pub fn serialize_saved_networks(
    saved_networks: Vec<wlan_policy::NetworkConfig>,
) -> Result<String, Error> {
    let simplified_saved_networks = saved_networks
        .iter()
        .filter_map(|network| match SimplifiedNetworkConfig::try_from(network.clone()) {
            Ok(network) => Some(network),
            Err(_e) => None,
        })
        .collect();
    let dump = WlanConfigDump { version: 1, data: simplified_saved_networks };
    let json = serde_json::to_string(&dump)?;
    // Percent-encode the data to get rid of characters that are problematic for piping in shells
    const FRAGMENT: &AsciiSet = &CONTROLS.add(b' ').add(b'"').add(b'<').add(b'>').add(b'`');
    let percent_encoded = utf8_percent_encode(&json, FRAGMENT).to_string();
    Ok(percent_encoded)
}

pub fn deserialize_saved_networks(
    raw_data: String,
) -> Result<Vec<wlan_policy::NetworkConfig>, Error> {
    let json = percent_decode_str(&raw_data).decode_utf8()?;
    let data: WlanConfigDump =
        serde_json::from_str(&json).map_err(|e| format_err!("Failed to parse config: {}", e))?;
    let networks: Vec<wlan_policy::NetworkConfig> =
        data.data.iter().map(|network| wlan_policy::NetworkConfig::from(network.clone())).collect();
    Ok(networks)
}

#[cfg(test)]
mod tests {
    use {super::*, ieee80211::Ssid};

    fn generate_test_data() -> (String, Vec<wlan_policy::NetworkConfig>) {
        let serialized = "{%22version%22:1,%22data%22:[{%22ssid%22:[84,101,115,116,87,76,65,78,49],%22credential%22:{%22Password%22:[49,50,51,52,53,54,55,56]},%22security_type%22:%22Wpa2%22}]}".to_string();

        let deserialized: Vec<wlan_policy::NetworkConfig> = vec![wlan_policy::NetworkConfig {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: Ssid::from("TestWLAN1").into(),
                type_: wlan_policy::SecurityType::Wpa2,
            }),
            credential: Some(wlan_policy::Credential::Password("12345678".as_bytes().to_vec())),
            ..wlan_policy::NetworkConfig::EMPTY
        }];

        (serialized, deserialized)
    }

    #[fuchsia::test]
    fn test_serialization() {
        let (serialized, deserialized) = generate_test_data();
        assert_eq!(serialized, serialize_saved_networks(deserialized).unwrap());
    }

    #[fuchsia::test]
    fn test_serialization_with_malformed_network() {
        let (serialized, mut deserialized) = generate_test_data();
        // Add another network that's malformed (has no credential field)
        let mut malformed = vec![wlan_policy::NetworkConfig {
            id: Some(wlan_policy::NetworkIdentifier {
                ssid: Ssid::from("MALFORMED - NO CREDENTIAL").into(),
                type_: wlan_policy::SecurityType::Wpa2,
            }),
            ..wlan_policy::NetworkConfig::EMPTY
        }];
        deserialized.append(&mut malformed);
        // Only the correctly-formed network should be serialized
        assert_eq!(serialized, serialize_saved_networks(deserialized).unwrap());
    }

    #[fuchsia::test]
    fn test_deserialization() {
        let (serialized, deserialized) = generate_test_data();
        assert_eq!(deserialized, deserialize_saved_networks(serialized).unwrap());
    }
}
