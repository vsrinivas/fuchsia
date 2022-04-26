// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{
    de::{Error, Unexpected},
    Deserialize, Serialize,
};

const VERSION_HISTORY_BYTES: &[u8] = include_bytes!(env!("SDK_VERSION_HISTORY"));
const VERSION_HISTORY_SCHEMA_ID: &str = "https://fuchsia.dev/schema/version_history-ef02ef45.json";
const VERSION_HISTORY_NAME: &str = "Platform version map";
const VERSION_HISTORY_TYPE: &str = "version_history";

#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Clone, Serialize, Deserialize)]
pub struct Version {
    #[serde(with = "serde_u64")]
    pub api_level: u64,
    #[serde(with = "serde_hex")]
    pub abi_revision: u64,
}

#[derive(Serialize, Deserialize)]
struct VersionHistoryData {
    name: String,
    #[serde(rename = "type")]
    element_type: String,
    versions: Vec<Version>,
}

#[derive(Serialize, Deserialize)]
struct VersionHistory {
    schema_id: String,
    data: VersionHistoryData,
}

pub fn version_history() -> Result<Vec<Version>, serde_json::Error> {
    parse_version_history(VERSION_HISTORY_BYTES)
}

fn parse_version_history(bytes: &[u8]) -> Result<Vec<Version>, serde_json::Error> {
    let v: VersionHistory = serde_json::from_slice(bytes)?;
    if v.schema_id != VERSION_HISTORY_SCHEMA_ID {
        return Err(serde_json::Error::invalid_value(
            Unexpected::Str(&v.schema_id),
            &VERSION_HISTORY_SCHEMA_ID,
        ));
    }
    if v.data.name != VERSION_HISTORY_NAME {
        return Err(serde_json::Error::invalid_value(
            Unexpected::Str(&v.data.name),
            &VERSION_HISTORY_NAME,
        ));
    }
    if v.data.element_type != VERSION_HISTORY_TYPE {
        return Err(serde_json::Error::invalid_value(
            Unexpected::Str(&v.data.element_type),
            &VERSION_HISTORY_TYPE,
        ));
    }

    Ok(v.data.versions)
}

// Helpers to serialize and deserialize integer strings into u64s.
mod serde_u64 {
    use {
        serde::{
            de::{Error, Unexpected},
            Deserialize, Deserializer, Serialize, Serializer,
        },
        std::str::FromStr,
    };

    pub fn serialize<S>(value: &u64, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        format!("{}", value).serialize(serializer)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<u64, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        u64::from_str(&s)
            .map_err(|_| D::Error::invalid_value(Unexpected::Str(&s), &"an unsigned integer"))
    }
}

// Helpers to serialize and deserialize hex strings into u64s.
mod serde_hex {
    use serde::{
        de::{Error, Unexpected},
        Deserialize, Deserializer, Serialize, Serializer,
    };

    pub fn serialize<S>(value: &u64, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        format!("{:#X}", value).serialize(serializer)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<u64, D::Error>
    where
        D: Deserializer<'de>,
    {
        let s = String::deserialize(deserializer)?;
        if let Some(s) = s.strip_prefix("0x") {
            u64::from_str_radix(&s, 16)
        } else {
            u64::from_str_radix(&s, 10)
        }
        .map_err(|_| D::Error::invalid_value(Unexpected::Str(&s), &"a hex unsigned integer"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_history_works() {
        let versions = version_history().unwrap();
        assert_eq!(versions[0], Version { api_level: 4, abi_revision: 0x601665C5B1A89C7F })
    }

    #[test]
    fn test_parse_history_works() {
        let expected_bytes = br#"{
            "data": {
                "name": "Platform version map",
                "type": "version_history",
                "versions": [
                    {
                        "api_level": "1",
                        "abi_revision": "10"
                    },
                    {
                        "api_level": "2",
                        "abi_revision": "0x20"
                    }
                ]
            },
            "schema_id": "https://fuchsia.dev/schema/version_history-ef02ef45.json"
        }"#;

        assert_eq!(
            parse_version_history(&expected_bytes[..]).unwrap(),
            vec![
                Version { api_level: 1, abi_revision: 10 },
                Version { api_level: 2, abi_revision: 0x20 },
            ],
        );
    }

    #[test]
    fn test_parse_history_rejects_invalid_schema() {
        let expected_bytes = br#"{
            "data": {
                "name": "Platform version map",
                "type": "version_history",
                "versions": []
            },
            "schema_id": "some-schema"
        }"#;

        assert_eq!(
            &parse_version_history(&expected_bytes[..]).unwrap_err().to_string(),
            "invalid value: string \"some-schema\", expected https://fuchsia.dev/schema/version_history-ef02ef45.json"
        );
    }

    #[test]
    fn test_parse_history_rejects_invalid_name() {
        let expected_bytes = br#"{
            "data": {
                "name": "some-name",
                "type": "version_history",
                "versions": []
            },
            "schema_id": "https://fuchsia.dev/schema/version_history-ef02ef45.json"
        }"#;

        assert_eq!(
            &parse_version_history(&expected_bytes[..]).unwrap_err().to_string(),
            "invalid value: string \"some-name\", expected Platform version map"
        );
    }

    #[test]
    fn test_parse_history_rejects_invalid_type() {
        let expected_bytes = br#"{
            "data": {
                "name": "Platform version map",
                "type": "some-type",
                "versions": []
            },
            "schema_id": "https://fuchsia.dev/schema/version_history-ef02ef45.json"
        }"#;

        assert_eq!(
            &parse_version_history(&expected_bytes[..]).unwrap_err().to_string(),
            "invalid value: string \"some-type\", expected version_history"
        );
    }

    #[test]
    fn test_parse_history_rejects_invalid_versions() {
        for (api_level, abi_revision, err) in [
            (
                "some-version",
                "1",
                "invalid value: string \"some-version\", expected an unsigned integer at line 1 column 123",
            ),
            (
                "-1",
                "1",
                 "invalid value: string \"-1\", expected an unsigned integer at line 1 column 113",
            ),
            (
                "1",
                "some-revision",
                "invalid value: string \"some-revision\", expected a hex unsigned integer at line 1 column 107",
            ),
            (
                "1",
                "-1",
                "invalid value: string \"-1\", expected a hex unsigned integer at line 1 column 96",
            ),
        ] {
            let expected_bytes = serde_json::to_vec(&serde_json::json!({
                "data": {
                    "name": VERSION_HISTORY_NAME,
                    "type": VERSION_HISTORY_TYPE,
                    "versions": [
                        {
                            "api_level": api_level,
                            "abi_revision": abi_revision,
                        },
                    ],
                },
                "schema_id": VERSION_HISTORY_SCHEMA_ID,
            }))
            .unwrap();

            assert_eq!(parse_version_history(&expected_bytes[..]).unwrap_err().to_string(), err);
        }
    }
}
