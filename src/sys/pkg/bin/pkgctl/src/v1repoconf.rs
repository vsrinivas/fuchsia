// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a temporary solution to use v1 repository configs in pkgctl
// until there is no longer a need to accept v1 repository configs.

use {
    fidl_fuchsia_pkg as fidl,
    serde::{Deserialize, Serialize},
    std::borrow::Cow,
};

#[derive(Debug, Serialize, Deserialize)]
#[allow(non_snake_case)]
pub struct KeyConfig {
    r#type: String,
    #[serde(with = "hex_serde")]
    value: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize)]
#[allow(non_snake_case)]
pub struct StatusConfig {
    Enabled: bool,
}

#[derive(Debug, Serialize, Deserialize)]
#[allow(non_snake_case)]
pub struct BlobEncryptionKey {
    #[serde(with = "hex_serde")]
    Data: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize)]
#[allow(non_snake_case)]
pub struct SourceConfig {
    ID: String,
    RepoURL: String,
    BlobRepoURL: String,
    RatePeriod: i64,
    RootKeys: Vec<KeyConfig>,
    #[serde(default = "default_root_version")]
    rootVersion: u32,
    #[serde(default = "default_root_threshold")]
    rootThreshold: u32,
    StatusConfig: StatusConfig,
    Auto: bool,
    BlobKey: Option<BlobEncryptionKey>,
}

impl SourceConfig {
    pub fn set_id(&mut self, id: &str) {
        self.ID = id.to_string();
    }
}

impl From<SourceConfig> for fidl::RepositoryConfig {
    fn from(config: SourceConfig) -> Self {
        fidl::RepositoryConfig {
            repo_url: Some(format_repo_url(&config.ID)),
            root_version: Some(config.rootVersion),
            root_threshold: Some(config.rootThreshold),
            root_keys: Some(config.RootKeys.into_iter().map(|key| key.into()).collect()),
            mirrors: Some({
                [fidl::MirrorConfig {
                    mirror_url: Some(config.RepoURL),
                    subscribe: Some(config.Auto),
                    ..fidl::MirrorConfig::EMPTY
                }]
                .to_vec()
            }),
            ..fidl::RepositoryConfig::EMPTY
        }
    }
}

impl From<KeyConfig> for fidl::RepositoryKeyConfig {
    fn from(key: KeyConfig) -> Self {
        match key.r#type.as_str() {
            "ed25519" => fidl::RepositoryKeyConfig::Ed25519Key(key.value),
            _ => fidl::RepositoryKeyConfig::unknown(0, Default::default()),
        }
    }
}

fn default_root_version() -> u32 {
    1
}

fn default_root_threshold() -> u32 {
    1
}

fn format_repo_url<'a>(url: &'a str) -> String {
    // If the canonical prefix was already part of the command line argument provided,
    // don't sanitize this prefix part of the string.
    let id = if let Some(u) = url.strip_prefix("fuchsia-pkg://") { u } else { url };
    return format!("fuchsia-pkg://{}", sanitize_id(id));
}

fn sanitize_id<'a>(id: &'a str) -> Cow<'a, str> {
    return id
        .chars()
        .map(|c| match c {
            'A'..='Z' | 'a'..='z' | '0'..='9' | '-' => c,
            _ => '_',
        })
        .collect();
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
