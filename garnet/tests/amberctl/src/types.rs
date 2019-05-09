// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl_fuchsia_amber as fidl,
    serde_derive::{Deserialize, Serialize},
    std::{
        convert::{TryFrom, TryInto},
        fmt,
    },
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

    pub fn id(mut self, value: impl Into<String>) -> Self {
        self.config.id = value.into();
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
#[serde(deny_unknown_fields)]
pub struct SourceConfig {
    id: String,
    #[serde(rename = "repoURL")]
    repo_url: String,
    #[serde(rename = "blobRepoURL")]
    blob_repo_url: String,
    #[serde(default, rename = "rateLimit")]
    rate_limit: u64,
    #[serde(default, rename = "ratePeriod")]
    rate_period: i32,
    #[serde(rename = "rootKeys")]
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
    pub fn repo_url(&self) -> &str {
        self.repo_url.as_str()
    }
}

impl Into<fidl::SourceConfig> for SourceConfig {
    fn into(self) -> fidl::SourceConfig {
        fidl::SourceConfig {
            id: self.id,
            repo_url: self.repo_url,
            blob_repo_url: self.blob_repo_url,
            rate_limit: self.rate_limit,
            rate_period: self.rate_period,
            root_keys: self.root_keys.into_iter().map(|key| key.into()).collect(),
            transport_config: self.transport_config.map(|cfg| Box::new(cfg.into())),
            oauth2_config: None,
            status_config: self.status_config.map(|cfg| Box::new(cfg.into())),
            auto: self.auto,
            blob_key: self.blob_key.map(|key| Box::new(key.into())),
        }
    }
}

impl TryFrom<fidl::SourceConfig> for SourceConfig {
    type Error = Error;
    fn try_from(x: fidl::SourceConfig) -> Result<Self, Self::Error> {
        if x.oauth2_config.is_some() {
            return Err(format_err!("oauth2_config is not supported"));
        }
        Ok(SourceConfig {
            id: x.id,
            repo_url: x.repo_url,
            blob_repo_url: x.blob_repo_url,
            rate_limit: x.rate_limit,
            rate_period: x.rate_period,
            root_keys: x
                .root_keys
                .into_iter()
                .map(|key| key.try_into())
                .collect::<Result<Vec<_>, _>>()?,
            transport_config: x.transport_config.map(|cfg| cfg.try_into()).transpose()?,
            status_config: x.status_config.map(|cfg| cfg.try_into()).transpose()?,
            auto: x.auto,
            blob_key: x.blob_key.map(|key| key.try_into()).transpose()?,
        })
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

impl Into<fidl::TransportConfig> for TransportConfig {
    fn into(self) -> fidl::TransportConfig {
        fidl::TransportConfig {
            disable_keep_alives: self.disable_keep_alives,
            keep_alive: self.keep_alive,
            max_idle_conns: self.max_idle_conns,
            max_idle_conns_per_host: self.max_idle_conns_per_host,
            connect_timeout: self.connect_timeout,
            request_timeout: self.request_timeout,
            idle_conn_timeout: self.idle_conn_timeout,
            response_header_timeout: self.response_header_timeout,
            expect_continue_timeout: self.expect_continue_timeout,
            tls_handshake_timeout: self.tls_handshake_timeout,
            tls_client_config: None,
        }
    }
}

impl TryFrom<Box<fidl::TransportConfig>> for TransportConfig {
    type Error = Error;
    fn try_from(x: Box<fidl::TransportConfig>) -> Result<Self, Self::Error> {
        if x.tls_client_config.is_some() {
            return Err(format_err!("tls_client_config not supported"));
        }
        Ok(TransportConfig {
            disable_keep_alives: x.disable_keep_alives,
            keep_alive: x.keep_alive,
            max_idle_conns: x.max_idle_conns,
            max_idle_conns_per_host: x.max_idle_conns_per_host,
            connect_timeout: x.connect_timeout,
            request_timeout: x.request_timeout,
            idle_conn_timeout: x.idle_conn_timeout,
            response_header_timeout: x.response_header_timeout,
            expect_continue_timeout: x.expect_continue_timeout,
            tls_handshake_timeout: x.tls_handshake_timeout,
        })
    }
}

#[derive(Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase", tag = "type", content = "value", deny_unknown_fields)]
pub enum KeyConfig {
    Ed25519(#[serde(with = "hex_serde")] Vec<u8>),
}

impl Into<fidl::KeyConfig> for KeyConfig {
    fn into(self) -> fidl::KeyConfig {
        let KeyConfig::Ed25519(value) = self;
        fidl::KeyConfig { type_: "ed25519".to_owned(), value: hex::encode(value) }
    }
}

impl TryFrom<fidl::KeyConfig> for KeyConfig {
    type Error = Error;
    fn try_from(x: fidl::KeyConfig) -> Result<Self, Self::Error> {
        if x.type_ != "ed25519" {
            return Err(format_err!("unknown key type: {}", x.type_));
        }
        Ok(KeyConfig::Ed25519(hex::decode(x.value.as_bytes())?))
    }
}

impl fmt::Debug for KeyConfig {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let KeyConfig::Ed25519(ref value) = self;
        f.write_str(&hex::encode(value))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct StatusConfig {
    enabled: bool,
}

impl Into<fidl::StatusConfig> for StatusConfig {
    fn into(self) -> fidl::StatusConfig {
        fidl::StatusConfig { enabled: self.enabled }
    }
}

impl TryFrom<Box<fidl::StatusConfig>> for StatusConfig {
    type Error = Error;
    fn try_from(x: Box<fidl::StatusConfig>) -> Result<Self, Self::Error> {
        Ok(StatusConfig { enabled: x.enabled })
    }
}

#[derive(Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct BlobEncryptionKey {
    data: [u8; 32],
}

impl Into<fidl::BlobEncryptionKey> for BlobEncryptionKey {
    fn into(self) -> fidl::BlobEncryptionKey {
        fidl::BlobEncryptionKey { data: self.data }
    }
}

impl TryFrom<Box<fidl::BlobEncryptionKey>> for BlobEncryptionKey {
    type Error = Error;
    fn try_from(x: Box<fidl::BlobEncryptionKey>) -> Result<Self, Self::Error> {
        Ok(BlobEncryptionKey { data: x.data })
    }
}

impl fmt::Debug for BlobEncryptionKey {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
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
