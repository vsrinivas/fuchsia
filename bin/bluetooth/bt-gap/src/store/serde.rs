// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These definitions are here to provide serde with the ability to derive Serialize/Deserialize
//! on bonding types declared in the FIDL crate.

// This allows us to provide a camel-case module name to the `derive_opt_box` macro below.
#![allow(non_snake_case)]

use fidl_fuchsia_bluetooth_control::{AddressType, BondingData, Key, LeConnectionParameters,
                                     LeData, Ltk, SecurityProperties};
use serde_derive::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeConnectionParameters")]
#[serde(rename_all = "camelCase")]
struct LeConnectionParametersDef {
    pub connection_interval: u16,
    pub connection_latency: u16,
    pub supervision_timeout: u16,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Key")]
#[serde(rename_all = "camelCase")]
struct KeyDef {
    #[serde(with = "SecurityPropertiesDef")]
    pub security_properties: SecurityProperties,
    pub value: [u8; 16],
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "SecurityProperties")]
#[serde(rename_all = "camelCase")]
struct SecurityPropertiesDef {
    pub authenticated: bool,
    pub secure_connections: bool,
    pub encryption_key_size: u8,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "AddressType")]
#[serde(rename_all = "camelCase")]
#[allow(dead_code)] // Workaround for rustc warning
enum AddressTypeDef {
    LePublic = 0,
    LeRandom = 1,
    Bredr = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Ltk")]
#[serde(rename_all = "camelCase")]
struct LtkDef {
    #[serde(with = "KeyDef")]
    pub key: Key,
    pub key_size: u8,
    pub ediv: u16,
    pub rand: u64,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeData")]
#[serde(rename_all = "camelCase")]
struct LeDataDef {
    pub address: String,
    #[serde(with = "AddressTypeDef")]
    pub address_type: AddressType,
    #[serde(with = "OptBoxLeConnectionParameters")]
    pub connection_parameters: Option<Box<LeConnectionParameters>>,
    pub services: Vec<String>,
    #[serde(with = "OptBoxLtk")]
    pub ltk: Option<Box<Ltk>>,
    #[serde(with = "OptBoxKey")]
    pub irk: Option<Box<Key>>,
    #[serde(with = "OptBoxKey")]
    pub csrk: Option<Box<Key>>,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "BondingData")]
#[serde(rename_all = "camelCase")]
#[allow(dead_code)] // Workaround for rustc warning
struct BondingDataDef {
    pub identifier: String,
    pub local_address: String,
    pub name: Option<String>,
    #[serde(with = "OptBoxLeData")]
    pub le: Option<Box<LeData>>,
}

#[derive(Serialize)]
pub struct BondingDataSerializer<'a>(#[serde(with = "BondingDataDef")] pub &'a BondingData);

#[derive(Deserialize)]
pub struct BondingDataDeserializer(#[serde(with = "BondingDataDef")] BondingData);

impl BondingDataDeserializer {
    pub fn contents(self) -> BondingData {
        self.0
    }
}

/// Expands to a mod that can be used by serde's "with" attribute to process Option<Box<T>>
/// where T is a remote type and can be serialized based on a local "TDef" type.
macro_rules! derive_opt_box {
    ($module:ident, $remote_type:ident, $local_type:ident, $local_type_str:expr) => {
        mod $module {
            use super::{$local_type, $remote_type};
            use serde::{Deserialize, Deserializer};
            use serde::{Serialize, Serializer};

            pub fn serialize<S>(
                value: &Option<Box<$remote_type>>, serializer: S,
            ) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                #[derive(Serialize)]
                struct Wrapper<'a>(#[serde(with = $local_type_str)] &'a Box<$remote_type>);
                value.as_ref().map(Wrapper).serialize(serializer)
            }

            pub fn deserialize<'de, D>(
                deserializer: D,
            ) -> Result<Option<Box<$remote_type>>, D::Error>
            where
                D: Deserializer<'de>,
            {
                #[derive(Deserialize)]
                struct Wrapper(#[serde(with = $local_type_str)] $remote_type);

                let helper = Option::deserialize(deserializer)?;
                Ok(helper.map(|Wrapper(external)| Box::new(external)))
            }
        }
    };
}

derive_opt_box!(OptBoxKey, Key, KeyDef, "KeyDef");
derive_opt_box!(OptBoxLeData, LeData, LeDataDef, "LeDataDef");
derive_opt_box!(
    OptBoxLeConnectionParameters,
    LeConnectionParameters,
    LeConnectionParametersDef,
    "LeConnectionParametersDef"
);
derive_opt_box!(OptBoxLtk, Ltk, LtkDef, "LtkDef");

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json;

    // Builds a BondingData FIDL struct to use as the input in the serialize test and the expected
    // output in the deserialize test.
    fn build_bonding_data() -> BondingData {
        BondingData {
            identifier: "1234".to_string(),
            local_address: "AA:BB:CC:DD:EE:FF".to_string(),
            name: Some("Device Name".to_string()),
            le: Some(Box::new(LeData {
                address: "01:02:03:04:05:06".to_string(),
                address_type: AddressType::LeRandom,
                connection_parameters: Some(Box::new(LeConnectionParameters {
                    connection_interval: 1,
                    connection_latency: 2,
                    supervision_timeout: 3,
                })),
                services: vec!["1800".to_string(), "1801".to_string()],
                ltk: Some(Box::new(Ltk {
                    key: Key {
                        security_properties: SecurityProperties {
                            authenticated: true,
                            secure_connections: false,
                            encryption_key_size: 16,
                        },
                        value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                    },
                    key_size: 16,
                    ediv: 1,
                    rand: 2,
                })),
                irk: Some(Box::new(Key {
                    security_properties: SecurityProperties {
                        authenticated: true,
                        secure_connections: false,
                        encryption_key_size: 16,
                    },
                    value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                })),
                csrk: None,
            })),
        }
    }

    #[test]
    fn serialize() {
        let serialized = serde_json::to_string(&BondingDataSerializer(&build_bonding_data())).unwrap();
        let expected = "{\
             \"identifier\":\"1234\",\
             \"localAddress\":\"AA:BB:CC:DD:EE:FF\",\
             \"name\":\"Device Name\",\
             \"le\":{\
                 \"address\":\"01:02:03:04:05:06\",\
                 \"addressType\":\"leRandom\",\
                 \"connectionParameters\":{\
                     \"connectionInterval\":1,\
                     \"connectionLatency\":2,\
                     \"supervisionTimeout\":3\
                 },\
                 \"services\":[\"1800\",\"1801\"],\
                 \"ltk\":{\
                     \"key\":{\
                         \"securityProperties\":{\
                             \"authenticated\":true,\
                             \"secureConnections\":false,\
                             \"encryptionKeySize\":16\
                         },\
                         \"value\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]\
                     },\
                     \"keySize\":16,\
                     \"ediv\":1,\
                     \"rand\":2\
                },\
                \"irk\":{\
                     \"securityProperties\":{\
                         \"authenticated\":true,\
                         \"secureConnections\":false,\
                         \"encryptionKeySize\":16\
                     },\
                     \"value\":[16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]\
                },\
                \"csrk\":null\
             }\
        }";
        assert_eq!(expected, serialized);
    }

    #[test]
    fn deserialize() {
        let json_input = r#"{
             "identifier": "1234",
             "localAddress": "AA:BB:CC:DD:EE:FF",
             "name": "Device Name",
             "le": {
                 "address": "01:02:03:04:05:06",
                 "addressType": "leRandom",
                 "connectionParameters": {
                     "connectionInterval": 1,
                     "connectionLatency": 2,
                     "supervisionTimeout": 3
                 },
                 "services": ["1800", "1801"],
                 "ltk": {
                     "key": {
                         "securityProperties": {
                             "authenticated": true,
                             "secureConnections": false,
                             "encryptionKeySize": 16
                         },
                         "value": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]
                     },
                     "keySize": 16,
                     "ediv": 1,
                     "rand": 2
                },
                "irk": {
                     "securityProperties": {
                         "authenticated": true,
                         "secureConnections": false,
                         "encryptionKeySize": 16
                     },
                     "value": [16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]
                },
                "csrk": null
             }
        }"#;

        let deserialized: BondingDataDeserializer = serde_json::from_str(json_input).unwrap();
        let expected = build_bonding_data();
        assert_eq!(expected, deserialized.0);
    }
}
