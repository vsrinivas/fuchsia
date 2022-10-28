// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a temporary solution to use v1 repository configs in pkgctl
// until there is no longer a need to accept v1 repository configs.

use {
    anyhow::bail,
    fidl_fuchsia_pkg as fidl,
    fidl_fuchsia_pkg_ext::RepositoryStorageType,
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
    #[serde(default = "default_repo_storage_type")]
    RepoStorageType: RepositoryStorageType,
}

impl SourceConfig {
    pub fn get_id(&self) -> String {
        self.ID.to_string()
    }
    pub fn set_id(&mut self, id: &str) {
        self.ID = id.to_string();
    }
    pub fn set_repo_storage_type(&mut self, t: RepositoryStorageType) {
        self.RepoStorageType = t;
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
            storage_type: Some(config.RepoStorageType.into()),
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

fn default_repo_storage_type() -> RepositoryStorageType {
    RepositoryStorageType::Ephemeral
}

fn format_repo_url(url: &str) -> String {
    // If the canonical prefix was already part of the command line argument provided,
    // don't sanitize this prefix part of the string.
    let id = if let Some(u) = url.strip_prefix("fuchsia-pkg://") { u } else { url };
    format!("fuchsia-pkg://{}", sanitize_id(id))
}

fn sanitize_id(id: &str) -> Cow<'_, str> {
    // The sanitized ID is used for the hostname part which can only contain lowercase letters,
    // digits and hyphens: https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
    return id
        .chars()
        .map(|c| match c {
            'a'..='z' | '0'..='9' | '-' | '.' => c,
            'A'..='Z' => c.to_ascii_lowercase(),
            _ => '-',
        })
        .collect();
}

pub fn validate_host(host: &str) -> Result<(), anyhow::Error> {
    // Allow only [a-z0-9-.] groups delimited by dots, according to the spec:
    // https://fuchsia.dev/fuchsia-src/concepts/packages/package_url
    if !host.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '.') {
        bail!("repo hostname {} contains invalid characters, only [a-z0-9-.] are allowed.", host);
    }
    Ok(())
}

mod hex_serde {
    use serde::Deserialize;

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
    use super::*;

    #[test]
    fn test_valid_hosts() {
        let valid_hosts =
            &["fuchsia.com", "fuchsia-1.com", "riscv.fuchsia.com", "rv64.fuchsia.com"];
        let invalid_hosts = &[
            "FuChSiA.CoM",
            "FUCHSIA_1.com",
            "FUCHSIA_1.COM",
            "fuchsia-①.com",
            "RISCV.fuchsia.com",
            "RV64.fuchsia.com",
        ];
        for host in valid_hosts {
            assert!(validate_host(host).is_ok());
        }
        for host in invalid_hosts {
            assert!(validate_host(host).is_err());
        }
    }

    #[test]
    fn test_sanitize_id() {
        let test_cases = [
            ("fuchsia.com", "fuchsia.com"),
            ("fuchsia.com", "FUCHSIA.COM"),
            ("fuchsia-1.com", "fuchsia_1.com"),
            ("http---fuchsia.com-", "http://fuchsia.com/"),
        ];
        for (want, input) in test_cases {
            assert_eq!(want, sanitize_id(input));
        }
    }

    #[test]
    fn test_format_repo_url() {
        let test_cases = [
            ("fuchsia-pkg://fuchsia.com", "fuchsia.com"),
            ("fuchsia-pkg://fuchsia.com", "FUCHSIA.COM"),
            ("fuchsia-pkg://fuchsia-1.com", "fuchsia_1.com"),
            ("fuchsia-pkg://http---fuchsia.com-", "http://fuchsia.com/"),
        ];
        for (want, input) in test_cases {
            assert_eq!(want, format_repo_url(input));
        }
    }
}
