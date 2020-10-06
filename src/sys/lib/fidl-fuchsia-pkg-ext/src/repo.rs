// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::{MirrorConfigError, RepositoryParseError, RepositoryUrlParseError},
    fidl_fuchsia_pkg as fidl, fuchsia_inspect as inspect,
    fuchsia_url::pkg_url::{PkgUrl, RepoUrl},
    http::Uri,
    http_uri_ext::HttpUriExt as _,
    serde::{Deserialize, Serialize},
    std::convert::TryFrom,
    std::{fmt, mem},
};

/// Convenience wrapper for the FIDL RepositoryStorageType.
#[derive(Clone, Hash, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RepositoryStorageType {
    /// Store the repository in-memory. This metadata will be lost if the process or device is
    /// restarted.
    Ephemeral,
}

/// Convenience wrapper for the FIDL RepositoryKeyConfig type
#[derive(Clone, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "lowercase", tag = "type", content = "value", deny_unknown_fields)]
pub enum RepositoryKey {
    Ed25519(#[serde(with = "hex_serde")] Vec<u8>),
}

/// Convenience wrapper for the FIDL MirrorConfig type
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct MirrorConfig {
    mirror_url: http::Uri,
    subscribe: bool,
    blob_mirror_url: http::Uri,
}

pub struct MirrorConfigInspectState {
    _mirror_url_property: inspect::StringProperty,
    _subscribe_property: inspect::StringProperty,
    _blob_mirror_url_property: inspect::StringProperty,
    _node: inspect::Node,
}

impl MirrorConfig {
    // Guaranteed to always have a `scheme`.
    pub fn mirror_url(&self) -> &http::Uri {
        &self.mirror_url
    }
    pub fn subscribe(&self) -> bool {
        self.subscribe
    }

    // Guaranteed to always have a `scheme`.
    pub fn blob_mirror_url(&self) -> &http::Uri {
        &self.blob_mirror_url
    }
    pub fn create_inspect_state(&self, node: inspect::Node) -> MirrorConfigInspectState {
        MirrorConfigInspectState {
            _mirror_url_property: node
                .create_string("mirror_url", format!("{:?}", self.mirror_url)),
            _subscribe_property: node.create_string("subscribe", format!("{:?}", &self.subscribe)),
            _blob_mirror_url_property: node
                .create_string("blob_mirror_url", format!("{:?}", self.blob_mirror_url)),
            _node: node,
        }
    }
}

/// Omit empty optional fields and omit blob_mirror_url if derivable from mirror_url.
impl serde::Serialize for MirrorConfig {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        #[derive(Serialize)]
        pub struct SerMirrorConfig<'a> {
            #[serde(with = "uri_serde")]
            mirror_url: &'a http::Uri,
            subscribe: bool,
            #[serde(skip_serializing_if = "Option::is_none")]
            blob_mirror_url: Option<&'a str>,
        }

        let blob_mirror_url = normalize_blob_mirror_url(&self.mirror_url, &self.blob_mirror_url);
        let blob_mirror_string: String;
        let blob_mirror_url = if let Some(blob_mirror_url) = blob_mirror_url {
            blob_mirror_string = blob_mirror_url.to_string();
            Some(blob_mirror_string.as_ref())
        } else {
            None
        };
        SerMirrorConfig { mirror_url: &self.mirror_url, subscribe: self.subscribe, blob_mirror_url }
            .serialize(serializer)
    }
}

/// Derive blob_mirror_url from mirror_url if blob_mirror_url is not present.
impl<'de> serde::Deserialize<'de> for MirrorConfig {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        pub struct DeMirrorConfig {
            #[serde(with = "uri_serde")]
            mirror_url: http::Uri,
            subscribe: bool,
            blob_mirror_url: Option<String>,
        }

        let DeMirrorConfig { mirror_url, subscribe, blob_mirror_url } =
            DeMirrorConfig::deserialize(deserializer)?;

        if mirror_url.scheme_str().is_none() {
            return Err(serde::de::Error::custom(format!(
                "mirror_url must have a scheme: {:?}",
                mirror_url
            )));
        }

        let blob_mirror_url = if let Some(blob_mirror_url) = blob_mirror_url {
            blob_mirror_url.parse::<Uri>().map_err(|e| {
                serde::de::Error::custom(format!("bad uri string: {:?}: {}", blob_mirror_url, e))
            })?
        } else {
            blob_mirror_url_from_mirror_url(&mirror_url)
        };

        if blob_mirror_url.scheme_str().is_none() {
            return Err(serde::de::Error::custom(format!(
                "blob_mirror_url must have a scheme: {:?}",
                blob_mirror_url
            )));
        }

        Ok(Self { mirror_url, subscribe, blob_mirror_url })
    }
}

/// Convenience wrapper for generating [MirrorConfig] values.
#[derive(Clone, Debug)]
pub struct MirrorConfigBuilder {
    config: MirrorConfig,
}

impl MirrorConfigBuilder {
    pub fn new(mirror_url: impl Into<http::Uri>) -> Result<Self, MirrorConfigError> {
        let mirror_url = mirror_url.into();
        if mirror_url.scheme().is_none() {
            return Err(MirrorConfigError::MirrorUrlMissingScheme);
        }
        let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
        Ok(MirrorConfigBuilder {
            config: MirrorConfig { mirror_url, subscribe: false, blob_mirror_url },
        })
    }

    pub fn mirror_url(
        mut self,
        mirror_url: impl Into<http::Uri>,
    ) -> Result<Self, (Self, MirrorConfigError)> {
        self.config.mirror_url = mirror_url.into();
        if self.config.mirror_url.scheme().is_none() {
            return Err((self, MirrorConfigError::MirrorUrlMissingScheme));
        }
        Ok(self)
    }

    pub fn blob_mirror_url(
        mut self,
        blob_mirror_url: impl Into<http::Uri>,
    ) -> Result<Self, (Self, MirrorConfigError)> {
        self.config.blob_mirror_url = blob_mirror_url.into();
        if self.config.blob_mirror_url.scheme().is_none() {
            return Err((self, MirrorConfigError::BlobMirrorUrlMissingScheme));
        }
        Ok(self)
    }

    pub fn subscribe(mut self, subscribe: bool) -> Self {
        self.config.subscribe = subscribe;
        self
    }

    pub fn build(self) -> MirrorConfig {
        self.config
    }
}

impl From<MirrorConfigBuilder> for MirrorConfig {
    fn from(builder: MirrorConfigBuilder) -> Self {
        builder.build()
    }
}

impl TryFrom<fidl::MirrorConfig> for MirrorConfig {
    type Error = RepositoryParseError;
    fn try_from(other: fidl::MirrorConfig) -> Result<Self, RepositoryParseError> {
        let mirror_url =
            other.mirror_url.ok_or(RepositoryParseError::MirrorUrlMissing)?.parse::<Uri>()?;
        if mirror_url.scheme().is_none() {
            Err(MirrorConfigError::MirrorUrlMissingScheme)?
        }
        let blob_mirror_url = match other.blob_mirror_url {
            None => blob_mirror_url_from_mirror_url(&mirror_url),
            Some(s) => {
                let url = s.parse::<http::Uri>()?;
                if url.scheme().is_none() {
                    Err(MirrorConfigError::BlobMirrorUrlMissingScheme)?
                }
                url
            }
        };

        Ok(Self {
            mirror_url,
            subscribe: other.subscribe.ok_or(RepositoryParseError::SubscribeMissing)?,
            blob_mirror_url,
        })
    }
}

impl From<MirrorConfig> for fidl::MirrorConfig {
    fn from(config: MirrorConfig) -> Self {
        let blob_mirror_url =
            normalize_blob_mirror_url(&config.mirror_url, &config.blob_mirror_url)
                .map(|url| url.to_string());
        Self {
            mirror_url: Some(config.mirror_url.to_string()),
            subscribe: Some(config.subscribe),
            blob_mirror_url,
        }
    }
}

impl From<fidl::RepositoryStorageType> for RepositoryStorageType {
    fn from(other: fidl::RepositoryStorageType) -> Self {
        match other {
            fidl::RepositoryStorageType::Ephemeral => RepositoryStorageType::Ephemeral,
        }
    }
}

impl From<RepositoryStorageType> for fidl::RepositoryStorageType {
    fn from(storage_type: RepositoryStorageType) -> Self {
        match storage_type {
            RepositoryStorageType::Ephemeral => fidl::RepositoryStorageType::Ephemeral,
        }
    }
}

fn blob_mirror_url_from_mirror_url(mirror_url: &http::Uri) -> http::Uri {
    // Safe because mirror_url has a scheme and "blobs" is a valid path segment.
    mirror_url.to_owned().extend_dir_with_path("blobs").unwrap()
}

fn is_default_blob_mirror_url(mirror_url: &http::Uri, blob_mirror_url: &http::Uri) -> bool {
    blob_mirror_url == &blob_mirror_url_from_mirror_url(mirror_url)
}

fn normalize_blob_mirror_url<'a>(
    mirror_url: &http::Uri,
    blob_mirror_url: &'a http::Uri,
) -> Option<&'a http::Uri> {
    if is_default_blob_mirror_url(mirror_url, blob_mirror_url) {
        None
    } else {
        Some(blob_mirror_url)
    }
}

/// Convenience wrapper type for the autogenerated FIDL `RepositoryConfig`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct RepositoryConfig {
    repo_url: RepoUrl,
    #[serde(default = "default_root_version")]
    root_version: u32,
    #[serde(default = "default_root_threshold")]
    root_threshold: u32,
    root_keys: Vec<RepositoryKey>,
    mirrors: Vec<MirrorConfig>,
    update_package_url: Option<PkgUrl>,
    #[serde(default = "default_use_local_mirror")]
    use_local_mirror: bool,
    #[serde(default = "default_storage_type")]
    repo_storage_type: RepositoryStorageType,
}

fn default_root_version() -> u32 {
    1
}

fn default_root_threshold() -> u32 {
    1
}

fn default_use_local_mirror() -> bool {
    false
}

fn default_storage_type() -> RepositoryStorageType {
    RepositoryStorageType::Ephemeral
}

impl RepositoryConfig {
    pub fn repo_url(&self) -> &RepoUrl {
        &self.repo_url
    }

    /// Insert the provided mirror, returning any previous mirror with the same URL.
    pub fn insert_mirror(&mut self, mut mirror: MirrorConfig) -> Option<MirrorConfig> {
        if let Some(m) = self.mirrors.iter_mut().find(|m| m.mirror_url == mirror.mirror_url) {
            mem::swap(m, &mut mirror);
            Some(mirror)
        } else {
            self.mirrors.push(mirror);
            None
        }
    }

    /// Remove the requested mirror by url, returning the removed mirror, if it existed.
    pub fn remove_mirror(&mut self, mirror_url: &http::Uri) -> Option<MirrorConfig> {
        if let Some(pos) = self.mirrors.iter().position(|m| &m.mirror_url == mirror_url) {
            Some(self.mirrors.remove(pos))
        } else {
            None
        }
    }

    /// Returns a slice of all mirrors.
    pub fn mirrors(&self) -> &[MirrorConfig] {
        &self.mirrors
    }

    /// Returns the initial trusted root version.
    pub fn root_version(&self) -> u32 {
        self.root_version
    }

    /// Returns the threshold of root keys needed to sign the initial root metadata before it is
    /// considered trusted.
    pub fn root_threshold(&self) -> u32 {
        self.root_threshold
    }

    /// Returns a slice of all root keys.
    pub fn root_keys(&self) -> &[RepositoryKey] {
        &self.root_keys
    }

    pub fn update_package_url(&self) -> Option<&PkgUrl> {
        self.update_package_url.as_ref()
    }

    pub fn use_local_mirror(&self) -> bool {
        self.use_local_mirror
    }
}

impl TryFrom<fidl::RepositoryConfig> for RepositoryConfig {
    type Error = RepositoryParseError;

    fn try_from(other: fidl::RepositoryConfig) -> Result<Self, RepositoryParseError> {
        let repo_url: RepoUrl = other
            .repo_url
            .ok_or(RepositoryParseError::RepoUrlMissing)?
            .parse()
            .map_err(|err| RepositoryParseError::InvalidRepoUrl(err))?;

        let update_package_url = if let Some(url) = other.update_package_url {
            let url =
                url.parse().map_err(|err| RepositoryParseError::InvalidUpdatePackageUrl(err))?;
            Some(url)
        } else {
            None
        };

        let root_version = if let Some(root_version) = other.root_version {
            if root_version < 1 {
                return Err(RepositoryParseError::InvalidRootVersion(root_version));
            }
            root_version
        } else {
            1
        };

        let root_threshold = if let Some(root_threshold) = other.root_threshold {
            if root_threshold < 1 {
                return Err(RepositoryParseError::InvalidRootThreshold(root_threshold));
            }
            root_threshold
        } else {
            1
        };

        let storage_type = RepositoryStorageType::Ephemeral;

        Ok(Self {
            repo_url: repo_url,
            root_version: root_version,
            root_threshold: root_threshold,
            root_keys: other
                .root_keys
                .unwrap_or(vec![])
                .into_iter()
                .map(RepositoryKey::try_from)
                .collect::<Result<_, _>>()?,
            mirrors: other
                .mirrors
                .unwrap_or(vec![])
                .into_iter()
                .map(MirrorConfig::try_from)
                .collect::<Result<_, _>>()?,
            update_package_url: update_package_url,
            use_local_mirror: other.use_local_mirror.unwrap_or(false),
            repo_storage_type: storage_type.into(),
        })
    }
}

impl From<RepositoryConfig> for fidl::RepositoryConfig {
    fn from(config: RepositoryConfig) -> Self {
        Self {
            repo_url: Some(config.repo_url.to_string()),
            root_version: Some(config.root_version),
            root_threshold: Some(config.root_threshold),
            root_keys: Some(config.root_keys.into_iter().map(RepositoryKey::into).collect()),
            mirrors: Some(config.mirrors.into_iter().map(MirrorConfig::into).collect()),
            update_package_url: config.update_package_url.map(|url| url.to_string()),
            use_local_mirror: Some(config.use_local_mirror),
            storage_type: Some(RepositoryStorageType::into(config.repo_storage_type)),
        }
    }
}

impl From<RepositoryConfig> for RepositoryConfigBuilder {
    fn from(config: RepositoryConfig) -> Self {
        Self { config }
    }
}

/// Convenience wrapper for generating [RepositoryConfig] values.
#[derive(Clone, Debug)]
pub struct RepositoryConfigBuilder {
    config: RepositoryConfig,
}

impl RepositoryConfigBuilder {
    pub fn new(repo_url: RepoUrl) -> Self {
        RepositoryConfigBuilder {
            config: RepositoryConfig {
                repo_url,
                root_version: 1,
                root_threshold: 1,
                root_keys: vec![],
                mirrors: vec![],
                update_package_url: None,
                use_local_mirror: false,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            },
        }
    }

    pub fn repo_url(mut self, repo_url: RepoUrl) -> Self {
        self.config.repo_url = repo_url;
        self
    }

    pub fn root_version(mut self, root_version: u32) -> Self {
        self.config.root_version = root_version;
        self
    }

    pub fn root_threshold(mut self, root_threshold: u32) -> Self {
        self.config.root_threshold = root_threshold;
        self
    }

    pub fn add_root_key(mut self, key: RepositoryKey) -> Self {
        self.config.root_keys.push(key);
        self
    }

    pub fn add_mirror(mut self, mirror: impl Into<MirrorConfig>) -> Self {
        self.config.mirrors.push(mirror.into());
        self
    }

    pub fn update_package_url(mut self, url: PkgUrl) -> Self {
        self.config.update_package_url = Some(url);
        self
    }

    pub fn use_local_mirror(mut self, use_local_mirror: bool) -> Self {
        self.config.use_local_mirror = use_local_mirror;
        self
    }

    pub fn repo_storage_type(mut self, repo_storage_type: RepositoryStorageType) -> Self {
        self.config.repo_storage_type = repo_storage_type;
        self
    }

    pub fn build(self) -> RepositoryConfig {
        self.config
    }
}

impl From<RepositoryConfigBuilder> for RepositoryConfig {
    fn from(builder: RepositoryConfigBuilder) -> Self {
        builder.build()
    }
}

/// Wraper for serializing repository configs to the on-disk JSON format.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
pub enum RepositoryConfigs {
    #[serde(rename = "1")]
    Version1(Vec<RepositoryConfig>),
}

impl TryFrom<fidl::RepositoryKeyConfig> for RepositoryKey {
    type Error = RepositoryParseError;
    fn try_from(id: fidl::RepositoryKeyConfig) -> Result<Self, RepositoryParseError> {
        match id {
            fidl::RepositoryKeyConfig::Ed25519Key(key) => Ok(RepositoryKey::Ed25519(key)),
            _ => Err(RepositoryParseError::UnsupportedKeyType),
        }
    }
}

impl From<RepositoryKey> for fidl::RepositoryKeyConfig {
    fn from(key: RepositoryKey) -> Self {
        match key {
            RepositoryKey::Ed25519(key) => fidl::RepositoryKeyConfig::Ed25519Key(key),
        }
    }
}

impl fmt::Debug for RepositoryKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let RepositoryKey::Ed25519(ref value) = self;
        f.debug_tuple("Ed25519").field(&hex::encode(value)).finish()
    }
}

#[derive(Debug, PartialEq, Clone)]
/// Convenience wrapper type for the autogenerated FIDL `RepositoryUrl`.
pub struct RepositoryUrl {
    url: RepoUrl,
}

impl RepositoryUrl {
    pub fn url(&self) -> &RepoUrl {
        &self.url
    }
}

impl From<RepoUrl> for RepositoryUrl {
    fn from(url: RepoUrl) -> Self {
        Self { url }
    }
}

impl TryFrom<&fidl::RepositoryUrl> for RepositoryUrl {
    type Error = RepositoryUrlParseError;

    fn try_from(other: &fidl::RepositoryUrl) -> Result<Self, RepositoryUrlParseError> {
        Ok(Self { url: other.url.parse().map_err(RepositoryUrlParseError::InvalidRepoUrl)? })
    }
}

impl From<RepositoryUrl> for fidl::RepositoryUrl {
    fn from(url: RepositoryUrl) -> Self {
        Self { url: url.url.to_string() }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, matches::assert_matches, proptest::prelude::*, serde_json::json,
        std::convert::TryInto,
    };
    fn verify_json_serde<T>(expected_value: T, expected_json: serde_json::Value)
    where
        T: PartialEq + std::fmt::Debug,
        T: serde::Serialize,
        for<'de> T: serde::Deserialize<'de>,
    {
        let actual_json = serde_json::to_value(&expected_value).expect("value to serialize");
        assert_eq!(actual_json, expected_json);
        let actual_value = serde_json::from_value::<T>(actual_json).expect("json to deserialize");
        assert_eq!(actual_value, expected_value);
    }

    #[test]
    fn test_repository_key_serde() {
        verify_json_serde(
            RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]),
            json!({
                "type": "ed25519",
                "value": "f10f1003",
            }),
        );
        verify_json_serde(
            RepositoryKey::Ed25519(vec![0, 1, 2, 3]),
            json!({
                "type": "ed25519",
                "value": "00010203",
            }),
        );
    }

    #[test]
    fn test_repository_key_deserialize_with_bad_type() {
        let json = r#"{"type":"bogus","value":"00010203"}"#;
        let result = serde_json::from_str::<RepositoryKey>(json);
        let error_message = result.unwrap_err().to_string();
        assert!(
            error_message.contains("unknown variant `bogus`"),
            r#"Error message did not contain "unknown variant `bogus`", was "{}""#,
            error_message
        );
    }

    #[test]
    fn test_repository_key_into_fidl() {
        let key = RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]);
        let as_fidl: fidl::RepositoryKeyConfig = key.into();
        assert_eq!(as_fidl, fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3]))
    }

    #[test]
    fn test_repository_key_from_fidl() {
        let as_fidl = fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3]);
        assert_matches!(
            RepositoryKey::try_from(as_fidl),
            Ok(RepositoryKey::Ed25519(v)) if v == vec![0xf1, 15, 16, 3]
        );
    }

    #[test]
    fn test_repository_key_from_fidl_with_bad_type() {
        let as_fidl = fidl::RepositoryKeyConfig::__UnknownVariant {
            ordinal: 999,
            bytes: vec![],
            handles: vec![],
        };
        assert_matches!(
            RepositoryKey::try_from(as_fidl),
            Err(RepositoryParseError::UnsupportedKeyType)
        );
    }

    #[test]
    fn test_repository_key_into_from_fidl_roundtrip() {
        let key = RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]);
        let as_fidl: fidl::RepositoryKeyConfig = key.clone().into();
        assert_eq!(RepositoryKey::try_from(as_fidl).unwrap(), key,)
    }

    #[test]
    fn test_mirror_config_serde() {
        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com/".parse::<Uri>().unwrap())
                .unwrap()
                .build(),
            json!({
                "mirror_url": "http://example.com/",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com".parse::<Uri>().unwrap())
                .unwrap()
                .blob_mirror_url("http://example.com/subdir".parse::<Uri>().unwrap())
                .unwrap()
                .build(),
            json!({
                "mirror_url": "http://example.com/",
                "blob_mirror_url": "http://example.com/subdir",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com".parse::<Uri>().unwrap())
                .unwrap()
                .blob_mirror_url("http://example.com/blobs".parse::<Uri>().unwrap())
                .unwrap()
                .build(),
            json!({
                "mirror_url": "http://example.com/",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com".parse::<Uri>().unwrap())
                .unwrap()
                .subscribe(true)
                .build(),
            json!({
                "mirror_url": "http://example.com/",
                "subscribe": true,
            }),
        );

        {
            // A mirror config with a previously-accepted "blob_key" still parses.
            let json = json!({
                "mirror_url": "http://example.com/",
                "blob_key": {
                    "type": "aes",
                    "value": "00010203",
                },
                "subscribe": false,
            });
            assert_eq!(
                serde_json::from_value::<MirrorConfig>(json).expect("json to deserialize"),
                MirrorConfigBuilder::new("http://example.com".parse::<Uri>().unwrap())
                    .unwrap()
                    .build()
            );
        }
    }

    #[test]
    fn test_mirror_config_deserialize_rejects_urls_without_schemes() {
        let j = json!({
            "mirror_url": "example.com",
            "subscribe": false,
        });
        assert_matches!(
            serde_json::from_value::<MirrorConfig>(j),
            Err(e) if e.to_string().starts_with("mirror_url must have a scheme")
        );

        let j = json!({
            "mirror_url": "https://example.com",
            "subscribe": false,
            "blob_mirror_url": Some("example.com")
        });
        assert_matches!(
            serde_json::from_value::<MirrorConfig>(j),
            Err(e) if e.to_string().starts_with("blob_mirror_url must have a scheme")
        );
    }

    #[test]
    fn test_mirror_config_into_fidl() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo".parse::<Uri>().unwrap(),
            subscribe: true,
            blob_mirror_url: "http://example.com/tuf/repo/subdir/blobs".parse::<Uri>().unwrap(),
        };
        let as_fidl: fidl::MirrorConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo".into()),
                subscribe: Some(true),
                blob_mirror_url: Some("http://example.com/tuf/repo/subdir/blobs".into()),
            }
        );
    }

    #[test]
    fn test_mirror_config_into_fidl_normalizes_blob_mirror_url() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo".parse::<Uri>().unwrap(),
            subscribe: false,
            blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
        };
        let as_fidl: fidl::MirrorConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo".into()),
                subscribe: Some(false),
                blob_mirror_url: None,
            }
        );
    }

    #[test]
    fn test_mirror_config_from_fidl() {
        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("http://example.com/tuf/repo".into()),
            subscribe: Some(true),
            blob_mirror_url: Some("http://example.com/tuf/repo/subdir/blobs".into()),
        };
        assert_matches!(
            MirrorConfig::try_from(as_fidl),
            Ok(mirror_config) if mirror_config == MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".parse::<Uri>().unwrap(),
                subscribe: true,
                blob_mirror_url: "http://example.com/tuf/repo/subdir/blobs".parse::<Uri>().unwrap(),
            }
        );
    }

    #[test]
    fn test_mirror_config_from_fidl_rejects_urls_without_schemes() {
        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("example.com".into()),
            subscribe: Some(false),
            blob_mirror_url: None,
        };
        assert_matches!(
            MirrorConfig::try_from(as_fidl),
            Err(RepositoryParseError::MirrorConfig(MirrorConfigError::MirrorUrlMissingScheme))
        );

        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("https://example.com".into()),
            subscribe: Some(false),
            blob_mirror_url: Some("example.com".into()),
        };
        assert_matches!(
            MirrorConfig::try_from(as_fidl),
            Err(RepositoryParseError::MirrorConfig(MirrorConfigError::BlobMirrorUrlMissingScheme))
        );
    }

    #[test]
    fn test_mirror_config_from_fidl_populates_blob_mirror_url() {
        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("http://example.com/tuf/repo/".into()),
            subscribe: Some(false),
            blob_mirror_url: None,
        };
        assert_matches!(
            MirrorConfig::try_from(as_fidl),
            Ok(mirror_config) if mirror_config == MirrorConfig {
                mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
                subscribe: false,
                blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
            }
        );
    }

    prop_compose! {
        fn uri_with_adversarial_path()(path in "[p/]{0,6}") -> http::Uri
        {
            let mut parts = http::uri::Parts::default();
            parts.scheme = Some(http::uri::Scheme::HTTP);
            parts.authority = Some(http::uri::Authority::from_static("example.com"));
            parts.path_and_query = Some(path.parse().unwrap());
            http::Uri::from_parts(parts).unwrap()
        }
    }

    proptest! {
        #[test]
        fn blob_mirror_url_from_mirror_url_produces_default_blob_mirror_urls(
            mirror_url in uri_with_adversarial_path()
        ) {
            let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
            prop_assert!(is_default_blob_mirror_url(&mirror_url, &blob_mirror_url));
        }

        #[test]
        fn normalize_blob_mirror_url_detects_default_blob_mirror_url(
            mirror_url in uri_with_adversarial_path()
        ) {
            let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
            prop_assert_eq!(normalize_blob_mirror_url(&mirror_url, &blob_mirror_url), None);
            // also, swapped parameters should never return None
            prop_assert_ne!(normalize_blob_mirror_url(&blob_mirror_url, &mirror_url), None);
        }
    }

    #[test]
    fn test_mirror_config_into_from_fidl_roundtrip() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
            subscribe: true,
            blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
        };
        let as_fidl: fidl::MirrorConfig = config.clone().into();
        assert_eq!(MirrorConfig::try_from(as_fidl).unwrap(), config);
    }

    #[test]
    fn test_mirror_config_builder() {
        let builder =
            MirrorConfigBuilder::new("http://example.com/".parse::<Uri>().unwrap()).unwrap();
        assert_eq!(
            builder.clone().build(),
            MirrorConfig {
                mirror_url: "http://example.com/".parse::<Uri>().unwrap(),
                subscribe: false,
                blob_mirror_url: "http://example.com/blobs".parse::<Uri>().unwrap(),
            }
        );
        assert_eq!(
            builder
                .clone()
                .blob_mirror_url("http://example.com/a/b".parse::<Uri>().unwrap())
                .unwrap()
                .build(),
            MirrorConfig {
                mirror_url: "http://example.com/".parse::<Uri>().unwrap(),
                subscribe: false,
                blob_mirror_url: "http://example.com/a/b".parse::<Uri>().unwrap(),
            }
        );
        assert_eq!(
            builder.clone().mirror_url("http://127.0.0.1".parse::<Uri>().unwrap()).unwrap().build(),
            MirrorConfig {
                mirror_url: "http://127.0.0.1".parse::<Uri>().unwrap(),
                subscribe: false,
                blob_mirror_url: "http://example.com/blobs".parse::<Uri>().unwrap(),
            }
        );
        assert_eq!(
            builder.subscribe(true).build(),
            MirrorConfig {
                mirror_url: "http://example.com".parse::<Uri>().unwrap(),
                subscribe: true,
                blob_mirror_url: "http://example.com/blobs".parse::<Uri>().unwrap(),
            }
        );
    }

    #[test]
    fn test_mirror_config_builder_rejects_urls_without_schemes() {
        assert_matches!(
            MirrorConfigBuilder::new("example.com".parse::<Uri>().unwrap()),
            Err(MirrorConfigError::MirrorUrlMissingScheme)
        );

        let builder =
            MirrorConfigBuilder::new("http://example.com/".parse::<Uri>().unwrap()).unwrap();
        assert_matches!(
            builder.mirror_url("example.com".parse::<Uri>().unwrap()),
            Err((_, MirrorConfigError::MirrorUrlMissingScheme))
        );

        let builder =
            MirrorConfigBuilder::new("http://example.com/".parse::<Uri>().unwrap()).unwrap();
        assert_matches!(
            builder.blob_mirror_url("example.com".parse::<Uri>().unwrap()),
            Err((_, MirrorConfigError::BlobMirrorUrlMissingScheme))
        );
    }

    #[test]
    fn test_mirror_config_bad_uri() {
        let as_fidl =
            fidl::MirrorConfig { mirror_url: None, subscribe: Some(false), blob_mirror_url: None };
        assert_matches!(
            MirrorConfig::try_from(as_fidl),
            Err(RepositoryParseError::MirrorUrlMissing)
        );
    }

    #[test]
    fn test_repository_config_into_fidl() {
        let config = RepositoryConfig {
            repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
            root_version: 2,
            root_threshold: 2,
            root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3])],
            mirrors: vec![MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".parse::<Uri>().unwrap(),
                subscribe: true,
                blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
            }],
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: true,
            repo_storage_type: RepositoryStorageType::Ephemeral,
        };
        let as_fidl: fidl::RepositoryConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::RepositoryConfig {
                repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
                root_version: Some(2),
                root_threshold: Some(2),
                root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
                mirrors: Some(vec![fidl::MirrorConfig {
                    mirror_url: Some("http://example.com/tuf/repo".into()),
                    subscribe: Some(true),
                    blob_mirror_url: None,
                }]),
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
                use_local_mirror: Some(true),
                storage_type: Some(fidl::RepositoryStorageType::Ephemeral),
            }
        );
    }

    #[test]
    fn test_repository_config_from_fidl_without_storage_type() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
            root_version: Some(1),
            root_threshold: Some(1),
            root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
            mirrors: Some(vec![fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo/".into()),
                subscribe: Some(true),
                blob_mirror_url: None,
            }]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: None,
            storage_type: None,
        };
        assert_matches!(
            RepositoryConfig::try_from(as_fidl),
            Ok(repository_config) if repository_config == RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_version: 1,
                root_threshold: 1,
                root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]),],
                mirrors: vec![MirrorConfig {
                    mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
                    subscribe: true,
                    blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
                },],
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
                use_local_mirror: false,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            }
        );
    }

    #[test]
    fn test_repository_config_from_fidl_without_version_and_threshold_and_use_local_mirror() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
            root_version: None,
            root_threshold: None,
            root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
            mirrors: Some(vec![fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo/".into()),
                subscribe: Some(true),
                blob_mirror_url: None,
            }]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: None,
            storage_type: None,
        };
        assert_matches!(
            RepositoryConfig::try_from(as_fidl),
            Ok(repository_config) if repository_config == RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_version: 1,
                root_threshold: 1,
                root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]),],
                mirrors: vec![MirrorConfig {
                    mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
                    subscribe: true,
                    blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
                },],
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
                use_local_mirror: false,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            }
        );
    }

    #[test]
    fn test_repository_config_from_fidl_with_version_and_threshold_and_use_local_mirror() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
            root_version: Some(2),
            root_threshold: Some(2),
            root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
            mirrors: Some(vec![fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo/".into()),
                subscribe: Some(true),
                blob_mirror_url: None,
            }]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: Some(true),
            storage_type: None,
        };
        assert_matches!(
            RepositoryConfig::try_from(as_fidl),
            Ok(repository_config) if repository_config == RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_version: 2,
                root_threshold: 2,
                root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]),],
                mirrors: vec![MirrorConfig {
                    mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
                    subscribe: true,
                    blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
                },],
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
                use_local_mirror: true,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            }
        );
    }

    #[test]
    fn test_repository_config_from_fidl_repo_url_missing() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: None,
            root_version: None,
            root_threshold: None,
            root_keys: Some(vec![]),
            mirrors: Some(vec![]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: None,
            storage_type: None,
        };
        assert_matches!(
            RepositoryConfig::try_from(as_fidl),
            Err(RepositoryParseError::RepoUrlMissing)
        );
    }

    #[test]
    fn test_repository_config_into_from_fidl_roundtrip() {
        let config = RepositoryConfig {
            repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
            root_version: 2,
            root_threshold: 2,
            root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3])],
            mirrors: vec![MirrorConfig {
                mirror_url: "http://example.com/tuf/repo/".parse::<Uri>().unwrap(),
                subscribe: true,
                blob_mirror_url: "http://example.com/tuf/repo/blobs".parse::<Uri>().unwrap(),
            }],
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
            use_local_mirror: true,
            repo_storage_type: RepositoryStorageType::Ephemeral,
        };
        let as_fidl: fidl::RepositoryConfig = config.clone().into();
        assert_eq!(RepositoryConfig::try_from(as_fidl).unwrap(), config);
    }

    #[test]
    fn test_repository_config_deserialize_missing_root_version_and_threshold_and_use_local_mirror()
    {
        let json_value = json!({
            "repo_url": "fuchsia-pkg://fuchsia.com",
            "root_keys": [],
            "mirrors": [],
            "update_package_url": null,
        });
        let actual_config: RepositoryConfig = serde_json::from_value(json_value).unwrap();

        assert_eq!(
            actual_config,
            RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_version: 1,
                root_threshold: 1,
                root_keys: vec![],
                mirrors: vec![],
                update_package_url: None,
                use_local_mirror: false,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            },
        );
    }

    #[test]
    fn test_repository_configs_serde_simple() {
        verify_json_serde(
            RepositoryConfigs::Version1(vec![RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_version: 1,
                root_threshold: 1,
                root_keys: vec![],
                mirrors: vec![],
                update_package_url: None,
                use_local_mirror: true,
                repo_storage_type: RepositoryStorageType::Ephemeral,
            }]),
            json!({
                "version": "1",
                "content": [{
                    "repo_url": "fuchsia-pkg://fuchsia.com",
                    "root_version": 1,
                    "root_threshold": 1,
                    "root_keys": [],
                    "mirrors": [],
                    "update_package_url": null,
                    "use_local_mirror": true,
                    "repo_storage_type": "ephemeral",
                }],
            }),
        );
    }

    #[test]
    fn test_repository_url_into_fidl() {
        let url = RepositoryUrl { url: "fuchsia-pkg://fuchsia.com".parse().unwrap() };
        let as_fidl: fidl::RepositoryUrl = url.into();
        assert_eq!(as_fidl, fidl::RepositoryUrl { url: "fuchsia-pkg://fuchsia.com".to_owned() });
    }

    #[test]
    fn test_repository_url_from_fidl() {
        let as_fidl = fidl::RepositoryUrl { url: "fuchsia-pkg://fuchsia.com".to_owned() };
        assert_matches!(
            RepositoryUrl::try_from(&as_fidl),
            Ok(RepositoryUrl { url }) if url == "fuchsia-pkg://fuchsia.com".parse().unwrap()
        );
    }

    #[test]
    fn test_repository_url_from_fidl_with_bad_url() {
        let as_fidl = fidl::RepositoryUrl { url: "invalid-scheme://fuchsia.com".to_owned() };
        assert_matches!(
            RepositoryUrl::try_from(&as_fidl),
            Err(RepositoryUrlParseError::InvalidRepoUrl(
                fuchsia_url::pkg_url::ParseError::InvalidScheme
            ))
        );
    }

    #[test]
    fn test_repository_url_into_from_fidl_roundtrip() {
        let url = RepositoryUrl { url: "fuchsia-pkg://fuchsia.com".parse().unwrap() };
        let as_fidl: fidl::RepositoryUrl = url.clone().into();
        assert_eq!(RepositoryUrl::try_from(&as_fidl).unwrap(), url);
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

mod uri_serde {
    use {http::Uri, serde::Deserialize};

    pub fn serialize<S>(uri: &http::Uri, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let s = uri.to_string();
        serializer.serialize_str(&s)
    }

    pub fn deserialize<'de, D>(deserializer: D) -> Result<http::Uri, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let value = String::deserialize(deserializer)?;
        value
            .parse::<Uri>()
            .map_err(|e| serde::de::Error::custom(format!("bad uri value: {:?}: {}", value, e)))
    }
}
