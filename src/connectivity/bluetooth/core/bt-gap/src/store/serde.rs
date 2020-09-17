// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines data structures to Serialize/Deserialize certain types from
//! the fuchsia-bluetooth crate based on the JSON schema described in
//! //docs/concepts/bluetooth/bonding_data_format.md.
//!
//! The BondingDataSerializer, BondingDataDeserializer, HostDataSerializer, and
//! HostDataDeserializer types can be used to (de)serialize the
//! `fuchsia_bluetooth::types::BondingData` and `fuchsia_bluetooth::types::HostData` types.
//!
//! Examples:
//!
//!   // Serialize a BondingData:
//!   let bd: fuchsia_bluetooth::BondingData = ...;
//!   let json = serde_json::to_string(&BondingDataSerializer::new(&bd))?;
//!
//!   // Deserialize JSON into BondingData:
//!   let deserialized = BondingDataDeserializer::from_json(&json)?;
//!
//!   // Serialize a HostData:
//!   let hd: fuchsia_bluetooth::HostData = ...;
//!   let json = serde_json::to_string(&HostDataSerializer(&hd))?;
//!
//!   // Deserialize JSON into HostData:
//!   let deserialized = HostDataDeserializer::from_json(&json)?;

// This allows us to provide a camel-case module name to the `derive_opt_box` macro below.
#![allow(non_snake_case)]

use {
    fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::types::{
        Address, BondingData, BredrData, HostData, LeData, OneOrBoth, PeerId, Uuid,
    },
    serde::{Deserialize, Serialize},
    std::convert::{From, TryInto},
};

#[derive(Serialize)]
pub struct BondingDataSerializer(BondingDataDef);

impl BondingDataSerializer {
    pub fn new(bd: &BondingData) -> BondingDataSerializer {
        BondingDataSerializer(bd.into())
    }
}

#[derive(Deserialize)]
pub struct BondingDataDeserializer(BondingDataDef);

impl BondingDataDeserializer {
    pub fn from_json(json: &str) -> Result<BondingData, anyhow::Error> {
        let de: BondingDataDeserializer = serde_json::from_str(json)?;
        de.0.try_into()
    }
}

#[derive(Serialize)]
pub struct HostDataSerializer<'a>(#[serde(with = "HostDataDef")] pub &'a HostData);

#[derive(Deserialize)]
pub struct HostDataDeserializer(#[serde(with = "HostDataDef")] HostData);

impl HostDataDeserializer {
    pub fn from_json(json: &str) -> Result<HostData, anyhow::Error> {
        let de: HostDataDeserializer = serde_json::from_str(json)?;
        Ok(de.0)
    }
}

// Macro to generate a (de)serializer for option types with a local serde placeholder structure for
// an external type. Use it like so:
//
//   #[derive(Serialize, Deserialize)]
//   #[derive(remote = "MyType")]
//   struct MyTypeDef {
//     foo: String,
//   }
//   option_encoding!(OptionMyTypeDef, MyType, "MyTypeDef");
//
//   #[derive(Serialize, Deserialize)]
//   struct MyOtherTypeDef {
//      // A `MyType` can be (de)serialized with "MyTypeDef".
//      #[serde(with = "MyTypeDef")]
//      my_required_type: MyType,
//
//      // A `Option<MyType>` can be (de)serialized with "OptionMyTypeDef" which we declared above.
//      #[serde(with = "OptionMyTypeDef")]
//      my_optional_type: Option<MyType>,
//   }
macro_rules! option_encoding {
    ($encoding:ident, $remote_type:ty, $local_type:expr) => {
        mod $encoding {
            use {
                super::*,
                serde::{Deserialize, Deserializer, Serialize, Serializer},
            };

            pub fn serialize<S>(
                value: &Option<$remote_type>,
                serializer: S,
            ) -> Result<S::Ok, S::Error>
            where
                S: Serializer,
            {
                #[derive(Serialize)]
                struct Wrapper<'a>(#[serde(with = $local_type)] &'a $remote_type);
                value.as_ref().map(Wrapper).serialize(serializer)
            }

            pub fn deserialize<'de, D>(deserializer: D) -> Result<Option<$remote_type>, D::Error>
            where
                D: Deserializer<'de>,
            {
                #[derive(Deserialize)]
                struct Wrapper(#[serde(with = $local_type)] $remote_type);
                let helper = Option::deserialize(deserializer)?;
                Ok(helper.map(|Wrapper(external)| external))
            }
        }
    };
}

// Custom (de)serializer for fidl_fuchsia_bluetooth::ConnectionRole.
mod connection_role_encoding {
    use {
        fidl_fuchsia_bluetooth::ConnectionRole,
        serde::{de, Deserializer, Serializer},
    };

    pub fn serialize<S>(value: &ConnectionRole, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match value {
            ConnectionRole::Leader => serializer.serialize_str("leader"),
            ConnectionRole::Follower => serializer.serialize_str("follower"),
        }
    }

    struct Visitor;

    impl<'de> de::Visitor<'de> for Visitor {
        type Value = ConnectionRole;

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("ConnectionRole")
        }

        fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            match value {
                "leader" => Ok(ConnectionRole::Leader),
                "follower" => Ok(ConnectionRole::Follower),
                v => Err(de::Error::invalid_value(de::Unexpected::Str(v), &self)),
            }
        }
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<ConnectionRole, D::Error>
    where
        D: Deserializer<'de>,
    {
        deserializer.deserialize_str(Visitor)
    }
}

option_encoding!(OptionConnectionRoleDef, bt::ConnectionRole, "connection_role_encoding");

// Custom (de)serializer for `fuchsia_bluetooth::types::Address`.
mod address_encoding {
    use {
        fuchsia_bluetooth::types::Address,
        serde::ser::SerializeStruct,
        serde::{de, Deserialize, Deserializer, Serializer},
    };

    pub fn serialize<S>(address: &Address, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut state = serializer.serialize_struct("Address", 2)?;
        let (addr_type, addr_value) = match address {
            Address::Public(bytes) => ("public", bytes),
            Address::Random(bytes) => ("random", bytes),
        };
        state.serialize_field("type", addr_type)?;
        state.serialize_field("value", &addr_value)?;
        state.end()
    }

    #[derive(Deserialize)]
    #[serde(field_identifier, rename_all = "lowercase")]
    enum Field {
        Type,
        Value,
    }

    struct Visitor;

    impl<'de> de::Visitor<'de> for Visitor {
        type Value = Address;

        fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            formatter.write_str("Address")
        }

        fn visit_map<V>(self, mut map: V) -> Result<Self::Value, V::Error>
        where
            V: de::MapAccess<'de>,
        {
            let mut type_ = None;
            let mut value = None;
            while let Some(key) = map.next_key()? {
                match key {
                    Field::Type => {
                        if type_.is_some() {
                            return Err(de::Error::duplicate_field("type"));
                        }
                        type_ = Some(map.next_value()?);
                    }
                    Field::Value => {
                        if value.is_some() {
                            return Err(de::Error::duplicate_field("value"));
                        }
                        value = Some(map.next_value()?);
                    }
                }
            }
            let type_ = type_.ok_or_else(|| de::Error::missing_field("type"))?;
            let value = value.ok_or_else(|| de::Error::missing_field("value"))?;
            match type_ {
                "public" => Ok(Address::Public(value)),
                "random" => Ok(Address::Random(value)),
                v => Err(de::Error::invalid_value(de::Unexpected::Str(v), &self)),
            }
        }
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<Address, D::Error>
    where
        D: Deserializer<'de>,
    {
        const FIELDS: &'static [&'static str] = &["type", "value"];
        deserializer.deserialize_struct("Address", FIELDS, Visitor)
    }
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "sys::LeConnectionParameters")]
#[serde(rename_all = "camelCase")]
struct LeConnectionParametersDef {
    pub connection_interval: u16,
    pub connection_latency: u16,
    pub supervision_timeout: u16,
}
option_encoding!(
    OptionLeConnectionParametersDef,
    sys::LeConnectionParameters,
    "LeConnectionParametersDef"
);

#[derive(Serialize, Deserialize)]
#[serde(remote = "sys::Key")]
#[serde(rename_all = "camelCase")]
struct KeyDef {
    pub value: [u8; 16],
}
option_encoding!(OptionKeyDef, sys::Key, "KeyDef");

#[derive(Serialize, Deserialize)]
#[serde(remote = "sys::PeerKey")]
#[serde(rename_all = "camelCase")]
struct PeerKeyDef {
    #[serde(with = "SecurityPropertiesDef")]
    pub security: sys::SecurityProperties,
    #[serde(with = "KeyDef")]
    #[serde(flatten)]
    pub data: sys::Key,
}
option_encoding!(OptionPeerKeyDef, sys::PeerKey, "PeerKeyDef");

#[derive(Serialize, Deserialize)]
#[serde(remote = "sys::SecurityProperties")]
#[serde(rename_all = "camelCase")]
struct SecurityPropertiesDef {
    pub authenticated: bool,
    pub secure_connections: bool,
    pub encryption_key_size: u8,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "sys::Ltk")]
#[serde(rename_all = "camelCase")]
struct LtkDef {
    #[serde(with = "PeerKeyDef")]
    pub key: sys::PeerKey,
    pub ediv: u16,
    pub rand: u64,
}
option_encoding!(OptionLtkDef, sys::Ltk, "LtkDef");

// TODO(fxb/59274): Do not use Serde remote for FIDL tables.
#[derive(Serialize, Deserialize)]
#[serde(remote = "LeData")]
#[serde(rename_all = "camelCase")]
struct LeDataDef {
    #[serde(with = "OptionLeConnectionParametersDef")]
    pub connection_parameters: Option<sys::LeConnectionParameters>,
    #[serde(with = "OptionLtkDef")]
    pub peer_ltk: Option<sys::Ltk>,
    #[serde(with = "OptionLtkDef")]
    pub local_ltk: Option<sys::Ltk>,
    #[serde(with = "OptionPeerKeyDef")]
    pub irk: Option<sys::PeerKey>,
    #[serde(with = "OptionPeerKeyDef")]
    pub csrk: Option<sys::PeerKey>,
    #[serde(skip)]
    pub services: Vec<Uuid>,
}
option_encoding!(OptionLeDataDef, LeData, "LeDataDef");

// TODO(fxb/59274): Do not use Serde remote for FIDL tables.
#[derive(Serialize, Deserialize)]
#[serde(remote = "BredrData")]
#[serde(rename_all = "camelCase")]
struct BredrDataDef {
    #[serde(with = "OptionConnectionRoleDef")]
    pub role_preference: Option<bt::ConnectionRole>,
    #[serde(with = "OptionPeerKeyDef")]
    pub link_key: Option<sys::PeerKey>,
    pub services: Vec<Uuid>,
}
option_encoding!(OptionBredrDataDef, BredrData, "BredrDataDef");

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct BondingDataDef {
    pub identifier: u64,
    #[serde(with = "address_encoding")]
    pub address: Address,
    #[serde(with = "address_encoding")]
    #[serde(rename(serialize = "hostAddress", deserialize = "hostAddress"))]
    pub local_address: Address,
    pub name: Option<String>,
    #[serde(with = "OptionLeDataDef")]
    pub le: Option<LeData>,
    #[serde(with = "OptionBredrDataDef")]
    pub bredr: Option<BredrData>,
}

// The transport-specific data is stored in the library representation using the OneOrBoth type. We
// specially handle this type so that it can be flattened out to the separate "le" and "bredr"
// fields as specified in the JSON schema.
impl From<&BondingData> for BondingDataDef {
    fn from(src: &BondingData) -> BondingDataDef {
        let (le, bredr) = match &src.data {
            OneOrBoth::Left(le) => (Some(le.clone()), None),
            OneOrBoth::Right(bredr) => (None, Some(bredr.clone())),
            OneOrBoth::Both(le, bredr) => (Some(le.clone()), Some(bredr.clone())),
        };
        BondingDataDef {
            identifier: src.identifier.0,
            address: src.address.clone(),
            local_address: src.local_address.clone(),
            name: src.name.clone(),
            le,
            bredr,
        }
    }
}

// The transport-specific data is stored in the library representation using the OneOrBoth type. We
// explicitly construct this type out of the separate "le" and "bredr" fields specified in the
// JSON schema.
impl TryInto<BondingData> for BondingDataDef {
    type Error = anyhow::Error;
    fn try_into(self) -> Result<BondingData, Self::Error> {
        use anyhow::format_err;

        let data = match (self.le, self.bredr) {
            (Some(le), Some(bredr)) => OneOrBoth::Both(le, bredr),
            (Some(le), None) => OneOrBoth::Left(le),
            (None, Some(bredr)) => OneOrBoth::Right(bredr),
            (None, None) => {
                return Err(format_err!("transport specific bonding data required"));
            }
        };

        Ok(BondingData {
            identifier: PeerId(self.identifier),
            address: self.address,
            local_address: self.local_address,
            name: self.name,
            data,
        })
    }
}

// TODO(fxb/59274): Do not use Serde remote for FIDL tables.
#[derive(Serialize, Deserialize)]
#[serde(remote = "HostData")]
#[serde(rename_all = "camelCase")]
struct HostDataDef {
    #[serde(with = "OptionKeyDef")]
    pub irk: Option<sys::Key>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json;

    // Builds a BondingData FIDL struct to use as the input in the serialize test and the expected
    // output in the deserialize test.
    fn build_bonding_data() -> BondingData {
        BondingData {
            identifier: PeerId(1234),
            address: Address::Public([6, 5, 4, 3, 2, 1]),
            local_address: Address::Public([0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA]),
            name: Some("Device Name".to_string()),
            data: OneOrBoth::Both(
                LeData {
                    connection_parameters: Some(sys::LeConnectionParameters {
                        connection_interval: 1,
                        connection_latency: 2,
                        supervision_timeout: 3,
                    }),
                    services: vec![],
                    peer_ltk: Some(sys::Ltk {
                        key: sys::PeerKey {
                            security: sys::SecurityProperties {
                                authenticated: true,
                                secure_connections: false,
                                encryption_key_size: 16,
                            },
                            data: sys::Key {
                                value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                            },
                        },
                        ediv: 1,
                        rand: 2,
                    }),
                    local_ltk: Some(sys::Ltk {
                        key: sys::PeerKey {
                            security: sys::SecurityProperties {
                                authenticated: true,
                                secure_connections: false,
                                encryption_key_size: 16,
                            },
                            data: sys::Key {
                                value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                            },
                        },
                        ediv: 1,
                        rand: 2,
                    }),
                    irk: Some(sys::PeerKey {
                        security: sys::SecurityProperties {
                            authenticated: true,
                            secure_connections: false,
                            encryption_key_size: 16,
                        },
                        data: sys::Key {
                            value: [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1],
                        },
                    }),
                    csrk: None,
                },
                BredrData {
                    role_preference: Some(bt::ConnectionRole::Follower),
                    services: vec![Uuid::new16(0x110a), Uuid::new16(0x110b)],
                    link_key: Some(sys::PeerKey {
                        security: sys::SecurityProperties {
                            authenticated: true,
                            secure_connections: true,
                            encryption_key_size: 16,
                        },
                        data: sys::Key {
                            value: [9, 10, 11, 12, 13, 14, 15, 16, 1, 2, 3, 4, 5, 6, 7, 8],
                        },
                    }),
                },
            ),
        }
    }

    #[test]
    fn serialize_bonding_data() {
        let serialized =
            serde_json::to_string(&BondingDataSerializer::new(&build_bonding_data())).unwrap();
        #[rustfmt::skip]
        let expected = "{\
            \"identifier\":1234,\
            \"address\":{\
                \"type\":\"public\",\
                \"value\":[6,5,4,3,2,1]\
            },\
            \"hostAddress\":{\
                \"type\":\"public\",\
                \"value\":[255,238,221,204,187,170]\
            },\
            \"name\":\"Device Name\",\
            \"le\":{\
                \"connectionParameters\":{\
                    \"connectionInterval\":1,\
                    \"connectionLatency\":2,\
                    \"supervisionTimeout\":3\
                },\
                \"peerLtk\":{\
                    \"key\":{\
                        \"security\":{\
                            \"authenticated\":true,\
                            \"secureConnections\":false,\
                            \"encryptionKeySize\":16\
                        },\
                        \"value\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]\
                    },\
                    \"ediv\":1,\
                    \"rand\":2\
                },\
                \"localLtk\":{\
                    \"key\":{\
                        \"security\":{\
                            \"authenticated\":true,\
                            \"secureConnections\":false,\
                            \"encryptionKeySize\":16\
                        },\
                        \"value\":[16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]\
                    },\
                    \"ediv\":1,\
                    \"rand\":2\
                },\
                \"irk\":{\
                    \"security\":{\
                        \"authenticated\":true,\
                        \"secureConnections\":false,\
                        \"encryptionKeySize\":16\
                    },\
                    \"value\":[16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]\
                },\
                \"csrk\":null\
            },\
            \"bredr\":{\
                \"rolePreference\":\"follower\",\
                \"linkKey\":{\
                    \"security\":{\
                        \"authenticated\":true,\
                        \"secureConnections\":true,\
                        \"encryptionKeySize\":16\
                    },\
                    \"value\":[9,10,11,12,13,14,15,16,1,2,3,4,5,6,7,8]\
                },\
                \"services\":[\
                    \"0000110a-0000-1000-8000-00805f9b34fb\",\
                    \"0000110b-0000-1000-8000-00805f9b34fb\"\
                ]\
            }\
        }";
        assert_eq!(expected, serialized);
    }

    #[test]
    fn deserialize_bonding_data() {
        let json_input = r#"{
             "identifier": 1234,
             "address":{
                "type": "public",
                "value": [6,5,4,3,2,1]
             },
             "hostAddress":{
                "type": "public",
                "value": [255,238,221,204,187,170]
             },
             "name": "Device Name",
             "le": {
                 "connectionParameters": {
                     "connectionInterval": 1,
                     "connectionLatency": 2,
                     "supervisionTimeout": 3
                 },
                 "peerLtk": {
                     "key": {
                         "security": {
                             "authenticated": true,
                             "secureConnections": false,
                             "encryptionKeySize": 16
                         },
                         "value": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]
                     },
                     "ediv": 1,
                     "rand": 2
                },
                "localLtk": {
                     "key": {
                         "security": {
                             "authenticated": true,
                             "secureConnections": false,
                             "encryptionKeySize": 16
                         },
                         "value": [16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]
                     },
                     "ediv": 1,
                     "rand": 2
                },
                "irk": {
                     "security": {
                         "authenticated": true,
                         "secureConnections": false,
                         "encryptionKeySize": 16
                     },
                     "value": [16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1]
                },
                "csrk": null
             },
             "bredr": {
                 "rolePreference": "follower",
                 "linkKey": {
                     "security": {
                         "authenticated": true,
                         "secureConnections": true,
                         "encryptionKeySize": 16
                     },
                     "value": [9,10,11,12,13,14,15,16,1,2,3,4,5,6,7,8]
                 },
                 "services":[
                    "0000110a-0000-1000-8000-00805f9b34fb",
                    "0000110b-0000-1000-8000-00805f9b34fb"
                 ]
             }
        }"#;

        let deserialized = BondingDataDeserializer::from_json(json_input);
        let expected = build_bonding_data();
        assert_eq!(Ok(expected), deserialized.map_err(|e| format!("{:?}", e)));
    }

    #[test]
    fn transport_data_missing() {
        let json_input = r#"{
             "identifier": 1234,
             "address":{
                "type": "public",
                "value": [6,5,4,3,2,1]
             },
             "hostAddress":{
                "type": "public",
                "value": [255,238,221,204,187,170]
             },
             "name": "Device Name",
             "le": null,
             "bredr": null
        }"#;

        let deserialized = BondingDataDeserializer::from_json(json_input);
        assert_eq!(
            Err(format!("transport specific bonding data required")),
            deserialized.map_err(|e| format!("{:?}", e))
        );
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
            irk: Some(sys::Key { value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }),
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
        let deserialized = HostDataDeserializer::from_json(json_input).unwrap();
        let expected = HostData {
            irk: Some(sys::Key { value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }),
        };
        assert_eq!(expected, deserialized);
    }

    mod roundtrip {
        use super::*;
        use fuchsia_bluetooth::types::bonding_data::proptest_util::any_bonding_data;
        use proptest::prelude::*;

        proptest! {
            #[test]
            fn bonding_data(bonding_data in any_bonding_data()) {
                let serialized = serde_json::to_string(&BondingDataSerializer::new(&bonding_data)).unwrap();
                let deserialized = BondingDataDeserializer::from_json(&serialized).unwrap();
                assert_eq!(bonding_data, deserialized);
            }
        }
    }
}
