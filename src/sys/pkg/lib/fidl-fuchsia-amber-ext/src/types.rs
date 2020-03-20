// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    std::fmt,
};

#[cfg(test)]
use {
    proptest::prelude::{any, prop},
    proptest_derive::Arbitrary,
};

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct SourceConfigBuilder {
    config: SourceConfig,
}

impl SourceConfigBuilder {
    pub fn new(id: impl Into<String>) -> Self {
        Self {
            config: SourceConfig {
                id: id.into(),
                repo_url: "".to_string(),
                blob_repo_url: "".to_string(),
                rate_limit: 0,
                rate_period: 0,
                root_keys: vec![],
                transport_config: None,
                status_config: Some(StatusConfig { enabled: true }),
                auto: false,
                blob_key: None,
            },
        }
    }

    pub fn id(mut self, id: impl Into<String>) -> Self {
        self.config.id = id.into();
        self
    }

    pub fn repo_url(mut self, value: impl Into<String>) -> Self {
        self.config.repo_url = value.into();
        self.config.blob_repo_url = format!("{}/blobs", self.config.repo_url);
        self
    }

    pub fn rate_period(mut self, value: i32) -> Self {
        self.config.rate_period = value;
        self
    }

    pub fn auto(mut self, value: bool) -> Self {
        self.config.auto = value;
        self
    }

    pub fn add_root_key(mut self, value: &str) -> Self {
        self.config.root_keys.push(KeyConfig::Ed25519(hex::decode(value).unwrap()));
        self
    }

    pub fn enabled(mut self, enabled: bool) -> Self {
        self.config.status_config = Some(StatusConfig { enabled });
        self
    }

    pub fn build(self) -> SourceConfig {
        self.config
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[cfg_attr(test, derive(Arbitrary))]
#[serde(deny_unknown_fields)]
pub struct SourceConfig {
    #[cfg_attr(test, proptest(regex = "[[:alnum:]]+"))]
    id: String,

    #[serde(rename = "repoURL")]
    #[cfg_attr(test, proptest(strategy = "tests::arb_url()"))]
    repo_url: String,

    #[serde(rename = "blobRepoURL")]
    #[cfg_attr(test, proptest(strategy = "tests::arb_url()"))]
    blob_repo_url: String,

    #[serde(default, rename = "rateLimit")]
    rate_limit: u64,

    #[serde(default, rename = "ratePeriod")]
    rate_period: i32,

    #[serde(rename = "rootKeys")]
    #[cfg_attr(test, proptest(strategy = "prop::collection::vec(any::<KeyConfig>(), 1..5)"))]
    root_keys: Vec<KeyConfig>,

    #[serde(rename = "transportConfig")]
    transport_config: Option<TransportConfig>,

    #[serde(rename = "statusConfig")]
    status_config: Option<StatusConfig>,

    auto: bool,

    #[serde(rename = "blobKey")]
    blob_key: Option<BlobEncryptionKey>,
}

impl SourceConfig {
    pub fn id(&self) -> &str {
        self.id.as_str()
    }
}

impl Ord for SourceConfig {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.id.cmp(&other.id)
    }
}

impl PartialOrd for SourceConfig {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[cfg_attr(test, derive(Arbitrary))]
#[serde(deny_unknown_fields)]
pub struct TransportConfig {
    disable_keep_alives: bool,
    keep_alive: i32,
    max_idle_conns: i32,
    max_idle_conns_per_host: i32,
    connect_timeout: i32,
    request_timeout: i32,
    idle_conn_timeout: i32,
    response_header_timeout: i32,
    expect_continue_timeout: i32,
    tls_handshake_timeout: i32,
}

#[derive(Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[cfg_attr(test, derive(Arbitrary))]
#[serde(rename_all = "lowercase", tag = "type", content = "value", deny_unknown_fields)]
pub enum KeyConfig {
    Ed25519(#[serde(with = "hex_serde")] Vec<u8>),
}

impl fmt::Debug for KeyConfig {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let KeyConfig::Ed25519(ref value) = self;
        f.write_str(&hex::encode(value))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[cfg_attr(test, derive(Arbitrary))]
pub struct StatusConfig {
    enabled: bool,
}

#[derive(Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[cfg_attr(test, derive(Arbitrary))]
pub struct BlobEncryptionKey {
    data: [u8; 32],
}

impl fmt::Debug for BlobEncryptionKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&hex::encode(self.data))
    }
}

mod hex_serde {
    use {hex, serde::Deserialize};

    pub fn serialize<S>(bytes: &[u8], serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let s = hex::encode(bytes);
        serializer.serialize_str(&s)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        hex::decode(value.as_bytes())
            .map_err(|e| serde::de::Error::custom(format!("bad hex value: {:?}: {}", value, e)))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*};

    prop_compose! {
        pub(crate) fn arb_url()(
            scheme in "https?",
            host in prop::collection::vec("[[:word:]]+", 1..5),
            port: Option<u16>,
            path in prop::collection::vec("[[:word:]]+", 0..5)
        ) -> String {
            let mut res = format!("{}://{}", scheme, host.join("."));
            if let Some(port) = port {
                res.push_str(&format!(":{}", port));
            }
            if !path.is_empty() {
                res.push_str(&format!("/{}", path.join("/")));
            }
            res
        }
    }

    prop_compose! {
        fn arb_source_builder()(
            id in "[[:alnum:]]+",
            id2 in prop::option::of("[[:alnum:]]+"),
            repo_url in prop::option::of(arb_url()),
            rate_period in prop::option::of(any::<i32>()),
            auto in prop::option::of(any::<bool>()),
            root_keys in prop::collection::vec("[[:xdigit:]]{64}", 0..5),
            enabled in prop::option::of(any::<bool>()),
        ) -> SourceConfigBuilder {
            let mut builder = SourceConfigBuilder::new(id);
            if let Some(id2) = id2 {
                builder = builder.id(id2);
            }
            if let Some(repo_url) = repo_url {
                builder = builder.repo_url(repo_url);
            }
            if let Some(rate_period) = rate_period {
                builder = builder.rate_period(rate_period);
            }
            if let Some(auto) = auto {
                builder = builder.auto(auto);
            }
            for root_key in root_keys.into_iter() {
                builder = builder.add_root_key(root_key.as_str());
            }
            if let Some(enabled) = enabled {
                builder = builder.enabled(enabled);
            }
            builder
        }
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(64))]

        #[test]
        fn test_builder_builds(builder in arb_source_builder()) {
            builder.build()
        }

        #[test]
        fn test_json_roundtrips(config: SourceConfig) {
            let as_json = &serde_json::to_string(&config).unwrap();
            let same: SourceConfig = serde_json::from_str(as_json).unwrap();
            assert_eq!(same, config);
        }
    }
}
