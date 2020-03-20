// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These definitions are here to provide serde with the ability to derive Serialize/Deserialize
//! on bonding types declared in the FIDL crate.

// This allows us to provide a camel-case module name to the `derive_opt_box` macro below.
#![allow(non_snake_case)]

use {
    fidl_fuchsia_bluetooth_control::{
        AddressType, BondingData, BredrData, HostData, LeConnectionParameters, LeData, LocalKey,
        Ltk, RemoteKey, SecurityProperties,
    },
    serde::{Deserialize, Serialize},
};

#[derive(Serialize, Deserialize)]
#[serde(remote = "LeConnectionParameters")]
#[serde(rename_all = "camelCase")]
struct LeConnectionParametersDef {
    pub connection_interval: u16,
    pub connection_latency: u16,
    pub supervision_timeout: u16,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "RemoteKey")]
#[serde(rename_all = "camelCase")]
struct RemoteKeyDef {
    #[serde(with = "SecurityPropertiesDef")]
    pub security_properties: SecurityProperties,
    pub value: [u8; 16],
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "LocalKey")]
#[serde(rename_all = "camelCase")]
struct LocalKeyDef {
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
enum AddressTypeDef {
    LePublic = 0,
    LeRandom = 1,
    Bredr = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Ltk")]
#[serde(rename_all = "camelCase")]
struct LtkDef {
    #[serde(with = "RemoteKeyDef")]
    pub key: RemoteKey,
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
    pub irk: Option<Box<RemoteKey>>,
    #[serde(with = "OptBoxKey")]
    pub csrk: Option<Box<RemoteKey>>,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "BredrData")]
#[serde(rename_all = "camelCase")]
struct BredrDataDef {
    pub address: String,
    pub piconet_leader: bool,
    pub services: Vec<String>,
    #[serde(with = "OptBoxLtk")]
    pub link_key: Option<Box<Ltk>>,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "BondingData")]
#[serde(rename_all = "camelCase")]
struct BondingDataDef {
    pub identifier: String,
    pub local_address: String,
    pub name: Option<String>,
    #[serde(with = "OptBoxLeData")]
    pub le: Option<Box<LeData>>,
    #[serde(with = "OptBoxBredrData")]
    pub bredr: Option<Box<BredrData>>,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "HostData")]
#[serde(rename_all = "camelCase")]
struct HostDataDef {
    #[serde(with = "OptBoxLocalKey")]
    pub irk: Option<Box<LocalKey>>,
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

#[derive(Serialize)]
pub struct HostDataSerializer<'a>(#[serde(with = "HostDataDef")] pub &'a HostData);

#[derive(Deserialize)]
pub struct HostDataDeserializer(#[serde(with = "HostDataDef")] HostData);

impl HostDataDeserializer {
    pub fn contents(self) -> HostData {
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
                value: &Option<Box<$remote_type>>,
                serializer: S,
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

derive_opt_box!(OptBoxKey, RemoteKey, RemoteKeyDef, "RemoteKeyDef");
derive_opt_box!(OptBoxLeData, LeData, LeDataDef, "LeDataDef");
derive_opt_box!(OptBoxBredrData, BredrData, BredrDataDef, "BredrDataDef");
derive_opt_box!(
    OptBoxLeConnectionParameters,
    LeConnectionParameters,
    LeConnectionParametersDef,
    "LeConnectionParametersDef"
);
derive_opt_box!(OptBoxLtk, Ltk, LtkDef, "LtkDef");
derive_opt_box!(OptBoxLocalKey, LocalKey, LocalKeyDef, "LocalKeyDef");

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
                    key: RemoteKey {
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
                irk: Some(Box::new(RemoteKey {
                    security_properties: SecurityProperties {
                        authenticated: true,
                        secure_connections: false,
                        encryption_key_size: 16,
                    },
                    value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                })),
                csrk: None,
            })),
            bredr: Some(Box::new(BredrData {
                address: "06:05:04:03:02:01".to_string(),
                piconet_leader: false,
                services: vec!["1101".to_string(), "110A".to_string()],
                link_key: Some(Box::new(Ltk {
                    key: RemoteKey {
                        security_properties: SecurityProperties {
                            authenticated: true,
                            secure_connections: true,
                            encryption_key_size: 16,
                        },
                        value: [9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8],
                    },
                    key_size: 16,
                    ediv: 0,
                    rand: 0,
                })),
            })),
        }
    }

    #[test]
    fn serialize_bonding_data() {
        let serialized =
            serde_json::to_string(&BondingDataSerializer(&build_bonding_data())).unwrap();
        #[rustfmt::skip]
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
            },\
            \"bredr\":{\
                \"address\":\"06:05:04:03:02:01\",\
                \"piconetLeader\":false,\
                \"services\":[\"1101\",\"110A\"],\
                \"linkKey\":{\
                    \"key\":{\
                        \"securityProperties\":{\
                            \"authenticated\":true,\
                            \"secureConnections\":true,\
                            \"encryptionKeySize\":16\
                        },\
                        \"value\":[9,10,11,12,13,14,15,16,1,2,3,4,5,6,7,8]\
                    },\
                    \"keySize\":16,\
                    \"ediv\":0,\
                    \"rand\":0\
                }\
            }\
        }";
        assert_eq!(expected, serialized);
    }

    #[test]
    fn deserialize_bonding_data() {
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
             },
             "bredr": {
                 "address": "06:05:04:03:02:01",
                 "piconetLeader": false,
                 "services": ["1101", "110A"],
                 "linkKey": {
                     "key": {
                         "securityProperties": {
                             "authenticated": true,
                             "secureConnections": true,
                             "encryptionKeySize": 16
                         },
                         "value": [9,10,11,12,13,14,15,16,1,2,3,4,5,6,7,8]
                     },
                     "keySize": 16,
                     "ediv": 0,
                     "rand": 0
                }
             }
        }"#;

        let deserialized: BondingDataDeserializer = serde_json::from_str(json_input).unwrap();
        let expected = build_bonding_data();
        assert_eq!(expected, deserialized.0);
    }

    #[test]
    fn serialize_empty_host_data() {
        let host_data = HostData { irk: None };
        let serialized = serde_json::to_string(&HostDataSerializer(&host_data)).unwrap();
        let expected = "{\"irk\":null}";
        assert_eq!(expected, serialized);
    }

    #[test]
    fn serialize_host_data_with_irk() {
        let host_data = HostData {
            irk: Some(Box::new(LocalKey {
                value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
            })),
        };
        let serialized = serde_json::to_string(&HostDataSerializer(&host_data)).unwrap();
        let expected = "{\
                        \"irk\":{\"value\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}\
                        }";
        assert_eq!(expected, serialized);
    }

    #[test]
    fn deserialize_empty_host_data() {
        let json_input = "{\"irk\":null}";
        let deserialized: HostDataDeserializer = serde_json::from_str(json_input).unwrap();
        let expected = HostData { irk: None };
        assert_eq!(expected, deserialized.0);
    }

    #[test]
    fn deserialize_host_data_with_irk() {
        let json_input = r#"{
            "irk": {
                "value": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]
            }
        }"#;
        let deserialized: HostDataDeserializer = serde_json::from_str(json_input).unwrap();
        let expected = HostData {
            irk: Some(Box::new(LocalKey {
                value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
            })),
        };
        assert_eq!(expected, deserialized.0);
    }
}
