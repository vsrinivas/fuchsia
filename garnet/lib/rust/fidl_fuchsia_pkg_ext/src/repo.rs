// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::RepositoryParseError,
    fidl_fuchsia_pkg as fidl, fuchsia_inspect as inspect,
    fuchsia_url::pkg_url::{PkgUrl, RepoUrl},
    serde_derive::{Deserialize, Serialize},
    std::convert::TryFrom,
    std::{fmt, mem},
};

/// Convenience wrapper for the FIDL RepositoryKeyConfig type
#[derive(Clone, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "lowercase", tag = "type", content = "value", deny_unknown_fields)]
pub enum RepositoryKey {
    Ed25519(#[serde(with = "hex_serde")] Vec<u8>),
}

/// Convenience wrapper for the FIDL RepositoryBlobConfig type
#[derive(Clone, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "lowercase", tag = "type", content = "value", deny_unknown_fields)]
pub enum RepositoryBlobKey {
    Aes(#[serde(with = "hex_serde")] Vec<u8>),
}

/// Convenience wrapper for the FIDL MirrorConfig type
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct MirrorConfig {
    mirror_url: String,
    subscribe: bool,
    blob_key: Option<RepositoryBlobKey>,
    blob_mirror_url: String,
}

pub struct MirrorConfigInspectState {
    _mirror_url_property: inspect::StringProperty,
    _subscribe_property: inspect::StringProperty,
    _blob_key_property: inspect::StringProperty,
    _blob_mirror_url_property: inspect::StringProperty,
    _node: inspect::Node,
}

impl MirrorConfig {
    pub fn mirror_url(&self) -> &str {
        &self.mirror_url
    }
    pub fn subscribe(&self) -> bool {
        self.subscribe
    }
    pub fn blob_mirror_url(&self) -> &str {
        &self.blob_mirror_url
    }
    pub fn blob_key(&self) -> Option<&RepositoryBlobKey> {
        self.blob_key.as_ref()
    }
    pub fn create_inspect_state(&self, node: inspect::Node) -> MirrorConfigInspectState {
        MirrorConfigInspectState {
            _mirror_url_property: node
                .create_string("mirror_url", format!("{:?}", self.mirror_url)),
            _subscribe_property: node.create_string("subscribe", format!("{:?}", &self.subscribe)),
            _blob_key_property: node.create_string("blob_key", format!("{:?}", &self.blob_key)),
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
            mirror_url: &'a str,
            subscribe: bool,
            #[serde(skip_serializing_if = "Option::is_none")]
            blob_key: Option<&'a RepositoryBlobKey>,
            #[serde(skip_serializing_if = "Option::is_none")]
            blob_mirror_url: Option<&'a str>,
        }

        let blob_mirror_url =
            normalize_blob_mirror_url(&self.mirror_url, self.blob_mirror_url.as_ref());
        SerMirrorConfig {
            mirror_url: &self.mirror_url,
            subscribe: self.subscribe,
            blob_key: self.blob_key.as_ref(),
            blob_mirror_url,
        }
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
            mirror_url: String,
            subscribe: bool,
            blob_key: Option<RepositoryBlobKey>,
            blob_mirror_url: Option<String>,
        }
        let DeMirrorConfig { mirror_url, subscribe, blob_key, blob_mirror_url } =
            DeMirrorConfig::deserialize(deserializer)?;

        let blob_mirror_url =
            blob_mirror_url.unwrap_or_else(|| blob_mirror_url_from_mirror_url(&mirror_url));
        Ok(Self { mirror_url, subscribe, blob_key, blob_mirror_url })
    }
}

/// Convenience wrapper for generating [MirrorConfig] values.
#[derive(Clone, Debug)]
pub struct MirrorConfigBuilder {
    config: MirrorConfig,
}

impl MirrorConfigBuilder {
    pub fn new(mirror_url: impl Into<String>) -> Self {
        let mirror_url = mirror_url.into();
        let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
        MirrorConfigBuilder {
            config: MirrorConfig { mirror_url, subscribe: false, blob_key: None, blob_mirror_url },
        }
    }

    pub fn mirror_url(mut self, mirror_url: impl Into<String>) -> Self {
        self.config.mirror_url = mirror_url.into();
        self
    }

    pub fn blob_mirror_url(mut self, blob_mirror_url: impl Into<String>) -> Self {
        self.config.blob_mirror_url = blob_mirror_url.into();
        self
    }

    pub fn subscribe(mut self, subscribe: bool) -> Self {
        self.config.subscribe = subscribe;
        self
    }

    pub fn blob_key(mut self, blob_key: RepositoryBlobKey) -> Self {
        self.config.blob_key = Some(blob_key);
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
        let mirror_url = other.mirror_url.ok_or(RepositoryParseError::MirrorUrlMissing)?;
        let blob_mirror_url =
            other.blob_mirror_url.unwrap_or_else(|| blob_mirror_url_from_mirror_url(&mirror_url));
        Ok(Self {
            mirror_url,
            subscribe: other.subscribe.ok_or(RepositoryParseError::SubscribeMissing)?,
            blob_key: other.blob_key.map(RepositoryBlobKey::try_from).transpose()?,
            blob_mirror_url,
        })
    }
}

impl From<MirrorConfig> for fidl::MirrorConfig {
    fn from(config: MirrorConfig) -> Self {
        let blob_mirror_url = normalize_blob_mirror_url(&config.mirror_url, config.blob_mirror_url);
        Self {
            mirror_url: Some(config.mirror_url),
            subscribe: Some(config.subscribe),
            blob_key: config.blob_key.map(|k| k.into()),
            blob_mirror_url,
        }
    }
}

fn blob_mirror_url_from_mirror_url(mirror_url: &str) -> String {
    format!("{}/blobs", mirror_url)
}

fn is_default_blob_mirror_url(mirror_url: &str, blob_mirror_url: &str) -> bool {
    blob_mirror_url.starts_with(mirror_url) && &blob_mirror_url[mirror_url.len()..] == "/blobs"
}

fn normalize_blob_mirror_url<S: AsRef<str>>(mirror_url: &str, blob_mirror_url: S) -> Option<S> {
    if is_default_blob_mirror_url(mirror_url, blob_mirror_url.as_ref()) {
        None
    } else {
        Some(blob_mirror_url)
    }
}

/// Convenience wrapper type for the autogenerated FIDL `RepositoryConfig`.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct RepositoryConfig {
    repo_url: RepoUrl,
    root_keys: Vec<RepositoryKey>,
    mirrors: Vec<MirrorConfig>,
    update_package_url: Option<PkgUrl>,
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
    pub fn remove_mirror(&mut self, mirror_url: &str) -> Option<MirrorConfig> {
        if let Some(pos) = self.mirrors.iter().position(|m| m.mirror_url == mirror_url) {
            Some(self.mirrors.remove(pos))
        } else {
            None
        }
    }

    /// Returns a slice of all mirrors.
    pub fn mirrors(&self) -> &[MirrorConfig] {
        &self.mirrors
    }

    /// Returns a slice of all root keys.
    pub fn root_keys(&self) -> &[RepositoryKey] {
        &self.root_keys
    }

    pub fn update_package_url(&self) -> Option<&PkgUrl> {
        self.update_package_url.as_ref()
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

        Ok(Self {
            repo_url: repo_url,
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
        })
    }
}

impl From<RepositoryConfig> for fidl::RepositoryConfig {
    fn from(config: RepositoryConfig) -> Self {
        Self {
            repo_url: Some(config.repo_url.to_string()),
            root_keys: Some(config.root_keys.into_iter().map(RepositoryKey::into).collect()),
            mirrors: Some(config.mirrors.into_iter().map(MirrorConfig::into).collect()),
            update_package_url: config.update_package_url.map(|url| url.to_string()),
        }
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
                root_keys: vec![],
                mirrors: vec![],
                update_package_url: None,
            },
        }
    }

    pub fn repo_url(mut self, repo_url: RepoUrl) -> Self {
        self.config.repo_url = repo_url;
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
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let RepositoryKey::Ed25519(ref value) = self;
        f.debug_tuple("Ed25519").field(&hex::encode(value)).finish()
    }
}

impl TryFrom<fidl::RepositoryBlobKey> for RepositoryBlobKey {
    type Error = RepositoryParseError;
    fn try_from(id: fidl::RepositoryBlobKey) -> Result<Self, RepositoryParseError> {
        match id {
            fidl::RepositoryBlobKey::AesKey(key) => Ok(RepositoryBlobKey::Aes(key)),
            _ => Err(RepositoryParseError::UnsupportedKeyType),
        }
    }
}

impl From<RepositoryBlobKey> for fidl::RepositoryBlobKey {
    fn from(key: RepositoryBlobKey) -> Self {
        match key {
            RepositoryBlobKey::Aes(key) => fidl::RepositoryBlobKey::AesKey(key),
        }
    }
}

impl fmt::Debug for RepositoryBlobKey {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let RepositoryBlobKey::Aes(ref value) = self;
        f.debug_tuple("Aes").field(&hex::encode(value)).finish()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*, serde_json::json, std::convert::TryInto};

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
        assert_eq!(
            RepositoryKey::try_from(as_fidl),
            Ok(RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]))
        );
    }

    #[test]
    fn test_repository_key_from_fidl_with_bad_type() {
        let as_fidl = fidl::RepositoryKeyConfig::__UnknownVariant {
            ordinal: 999,
            bytes: vec![],
            handles: vec![],
        };
        assert_eq!(RepositoryKey::try_from(as_fidl), Err(RepositoryParseError::UnsupportedKeyType));
    }

    #[test]
    fn test_repository_key_into_from_fidl_roundtrip() {
        let key = RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]);
        let as_fidl: fidl::RepositoryKeyConfig = key.clone().into();
        assert_eq!(RepositoryKey::try_from(as_fidl).unwrap(), key,)
    }

    #[test]
    fn test_blob_key_into_fidl() {
        let key = RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3]);
        let as_fidl: fidl::RepositoryBlobKey = key.into();
        assert_eq!(as_fidl, fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3]))
    }

    #[test]
    fn test_blob_key_from_fidl() {
        let as_fidl = fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3]);
        assert_eq!(
            RepositoryBlobKey::try_from(as_fidl),
            Ok(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3]))
        );
    }

    #[test]
    fn test_blob_key_from_fidl_with_bad_type() {
        let as_fidl = fidl::RepositoryBlobKey::__UnknownVariant {
            ordinal: 999,
            bytes: vec![],
            handles: vec![],
        };
        assert_eq!(
            RepositoryBlobKey::try_from(as_fidl),
            Err(RepositoryParseError::UnsupportedKeyType)
        );
    }

    #[test]
    fn test_blob_key_into_from_fidl_roundtrip() {
        let key = RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3]);
        let as_fidl: fidl::RepositoryBlobKey = key.clone().into();
        assert_eq!(RepositoryBlobKey::try_from(as_fidl).unwrap(), key);
    }

    #[test]
    fn test_mirror_config_serde() {
        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com").build(),
            json!({
                "mirror_url": "http://example.com",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com")
                .blob_mirror_url("http://example.com/subdir")
                .build(),
            json!({
                "mirror_url": "http://example.com",
                "blob_mirror_url": "http://example.com/subdir",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com")
                .blob_mirror_url("http://example.com/blobs")
                .build(),
            json!({
                "mirror_url": "http://example.com",
                "subscribe": false,
            }),
        );

        verify_json_serde(
            MirrorConfigBuilder::new("http://example.com")
                .blob_key(RepositoryBlobKey::Aes(vec![0, 1, 2, 3]))
                .subscribe(true)
                .build(),
            json!({
                "mirror_url": "http://example.com",
                "blob_key": {
                    "type": "aes",
                    "value": "00010203",
                },
                "subscribe": true,
            }),
        );
    }

    #[test]
    fn test_mirror_config_into_fidl() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo".into(),
            subscribe: true,
            blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
            blob_mirror_url: "http://example.com/tuf/repo/subdir/blobs".into(),
        };
        let as_fidl: fidl::MirrorConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo".into()),
                subscribe: Some(true),
                blob_key: Some(fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: Some("http://example.com/tuf/repo/subdir/blobs".into()),
            }
        );
    }

    #[test]
    fn test_mirror_config_into_fidl_normalizes_blob_mirror_url() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo".into(),
            subscribe: false,
            blob_key: None,
            blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
        };
        let as_fidl: fidl::MirrorConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo".into()),
                subscribe: Some(false),
                blob_key: None,
                blob_mirror_url: None,
            }
        );
    }

    #[test]
    fn test_mirror_config_from_fidl() {
        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("http://example.com/tuf/repo".into()),
            subscribe: Some(true),
            blob_key: Some(fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3])),
            blob_mirror_url: Some("http://example.com/tuf/repo/subdir/blobs".into()),
        };
        assert_eq!(
            MirrorConfig::try_from(as_fidl),
            Ok(MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".into(),
                subscribe: true,
                blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: "http://example.com/tuf/repo/subdir/blobs".into(),
            })
        );
    }

    #[test]
    fn test_mirror_config_from_fidl_populates_blob_mirror_url() {
        let as_fidl = fidl::MirrorConfig {
            mirror_url: Some("http://example.com/tuf/repo".into()),
            subscribe: Some(false),
            blob_key: None,
            blob_mirror_url: None,
        };
        assert_eq!(
            MirrorConfig::try_from(as_fidl),
            Ok(MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".into(),
                subscribe: false,
                blob_key: None,
                blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
            })
        );
    }

    #[test]
    fn test_mirror_config_into_from_fidl_roundtrip() {
        let config = MirrorConfig {
            mirror_url: "http://example.com/tuf/repo".into(),
            subscribe: true,
            blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
            blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
        };
        let as_fidl: fidl::MirrorConfig = config.clone().into();
        assert_eq!(MirrorConfig::try_from(as_fidl).unwrap(), config);
    }

    proptest! {
        #[test]
        fn blob_mirror_url_from_mirror_url_produces_default_blob_mirror_urls(mirror_url: String) {
            let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
            prop_assert!(is_default_blob_mirror_url(&mirror_url, &blob_mirror_url));
        }

        #[test]
        fn is_default_blob_mirror_url_does_not_panic(a: String, b: String) {
            is_default_blob_mirror_url(&a, &b);
            is_default_blob_mirror_url(&b, &a);
        }

        #[test]
        fn normalize_blob_mirror_url_detects_default_blob_mirror_url(mirror_url: String) {
            let blob_mirror_url = blob_mirror_url_from_mirror_url(&mirror_url);
            prop_assert_eq!(normalize_blob_mirror_url(&mirror_url, blob_mirror_url.as_str()), None);
            // also, swapped parameters should never return None
            prop_assert_ne!(normalize_blob_mirror_url(&blob_mirror_url, mirror_url), None);
        }
    }

    #[test]
    fn test_mirror_config_builder() {
        let builder = MirrorConfigBuilder::new("http://example.com");
        assert_eq!(
            builder.clone().build(),
            MirrorConfig {
                mirror_url: "http://example.com".into(),
                subscribe: false,
                blob_key: None,
                blob_mirror_url: "http://example.com/blobs".into(),
            }
        );
        assert_eq!(
            builder.clone().blob_mirror_url("http://example.com/a/b").build(),
            MirrorConfig {
                mirror_url: "http://example.com".into(),
                subscribe: false,
                blob_key: None,
                blob_mirror_url: "http://example.com/a/b".into(),
            }
        );
        assert_eq!(
            builder.clone().mirror_url("http://127.0.0.1").build(),
            MirrorConfig {
                mirror_url: "http://127.0.0.1".into(),
                subscribe: false,
                blob_key: None,
                blob_mirror_url: "http://example.com/blobs".into(),
            }
        );
        assert_eq!(
            builder.subscribe(true).blob_key(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])).build(),
            MirrorConfig {
                mirror_url: "http://example.com".into(),
                subscribe: true,
                blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: "http://example.com/blobs".into(),
            }
        );
    }

    #[test]
    fn test_repository_config_into_fidl() {
        let config = RepositoryConfig {
            repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
            root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3])],
            mirrors: vec![MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".into(),
                subscribe: true,
                blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
            }],
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
        };
        let as_fidl: fidl::RepositoryConfig = config.into();
        assert_eq!(
            as_fidl,
            fidl::RepositoryConfig {
                repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
                root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
                mirrors: Some(vec![fidl::MirrorConfig {
                    mirror_url: Some("http://example.com/tuf/repo".into()),
                    subscribe: Some(true),
                    blob_key: Some(fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3])),
                    blob_mirror_url: None,
                }]),
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
            }
        );
    }

    #[test]
    fn test_repository_config_from_fidl() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: Some("fuchsia-pkg://fuchsia.com".try_into().unwrap()),
            root_keys: Some(vec![fidl::RepositoryKeyConfig::Ed25519Key(vec![0xf1, 15, 16, 3])]),
            mirrors: Some(vec![fidl::MirrorConfig {
                mirror_url: Some("http://example.com/tuf/repo".into()),
                subscribe: Some(true),
                blob_key: Some(fidl::RepositoryBlobKey::AesKey(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: None,
            }]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
        };
        assert_eq!(
            RepositoryConfig::try_from(as_fidl),
            Ok(RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3]),],
                mirrors: vec![MirrorConfig {
                    mirror_url: "http://example.com/tuf/repo".into(),
                    subscribe: true,
                    blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
                    blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
                },],
                update_package_url: Some(
                    "fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()
                ),
            })
        );
    }

    #[test]
    fn test_repository_config_from_fidl_repo_url_missing() {
        let as_fidl = fidl::RepositoryConfig {
            repo_url: None,
            root_keys: Some(vec![]),
            mirrors: Some(vec![]),
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
        };
        assert_eq!(RepositoryConfig::try_from(as_fidl), Err(RepositoryParseError::RepoUrlMissing));
    }

    #[test]
    fn test_repository_config_into_from_fidl_roundtrip() {
        let config = RepositoryConfig {
            repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
            root_keys: vec![RepositoryKey::Ed25519(vec![0xf1, 15, 16, 3])],
            mirrors: vec![MirrorConfig {
                mirror_url: "http://example.com/tuf/repo".into(),
                subscribe: true,
                blob_key: Some(RepositoryBlobKey::Aes(vec![0xf1, 15, 16, 3])),
                blob_mirror_url: "http://example.com/tuf/repo/blobs".into(),
            }],
            update_package_url: Some("fuchsia-pkg://fuchsia.com/systemupdate".try_into().unwrap()),
        };
        let as_fidl: fidl::RepositoryConfig = config.clone().into();
        assert_eq!(RepositoryConfig::try_from(as_fidl).unwrap(), config);
    }

    #[test]
    fn test_repository_configs_serde_simple() {
        verify_json_serde(
            RepositoryConfigs::Version1(vec![RepositoryConfig {
                repo_url: "fuchsia-pkg://fuchsia.com".try_into().unwrap(),
                root_keys: vec![],
                mirrors: vec![],
                update_package_url: None,
            }]),
            json!({
                "version": "1",
                "content": [{
                    "repo_url": "fuchsia-pkg://fuchsia.com",
                    "root_keys": [],
                    "mirrors": [],
                    "update_package_url": null,
                }],
            }),
        );
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
