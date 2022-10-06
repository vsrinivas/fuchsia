// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    data_encoding::HEXLOWER,
    serde::{Deserialize, Deserializer},
    std::{fmt, fs::File, io, path::Path},
    tuf::crypto::{Ed25519PrivateKey, KeyType, PrivateKey, PublicKey, SignatureScheme},
};

/// Errors returned by parsing keys.
#[derive(Debug, thiserror::Error)]
pub enum ParseError {
    /// IO error occurred.
    #[error(transparent)]
    Io(#[from] io::Error),

    /// TUF experienced an error parsing keys.
    #[error(transparent)]
    Tuf(#[from] tuf::Error),

    /// JSON parsing error.
    #[error(transparent)]
    Json(#[from] serde_json::Error),

    /// The private key's public key does not match the public key.
    #[error("private key's public key {expected:?} does not match public key {actual:?}")]
    PublicKeyDoesNotMatchPrivateKey { expected: PublicKey, actual: PublicKey },

    /// The key type and signature scheme is unsupported.
    #[error("unsupported key type {keytype:?} and signature scheme {scheme:?}")]
    UnsupportedKeyTypeAndScheme { keytype: KeyType, scheme: SignatureScheme },
}

/// Hold all the private keys for a repository.
pub struct RepoKeys {
    root_keys: Vec<Box<dyn PrivateKey>>,
    targets_keys: Vec<Box<dyn PrivateKey>>,
    snapshot_keys: Vec<Box<dyn PrivateKey>>,
    timestamp_keys: Vec<Box<dyn PrivateKey>>,
}

impl fmt::Debug for RepoKeys {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("RepoKeys")
            .field("root_keys", &self.root_keys.iter().map(|key| key.public()).collect::<Vec<_>>())
            .field(
                "targets_keys",
                &self.targets_keys.iter().map(|key| key.public()).collect::<Vec<_>>(),
            )
            .field(
                "snapshot_keys",
                &self.snapshot_keys.iter().map(|key| key.public()).collect::<Vec<_>>(),
            )
            .field(
                "timestamp_keys",
                &self.timestamp_keys.iter().map(|key| key.public()).collect::<Vec<_>>(),
            )
            .finish()
    }
}

impl RepoKeys {
    /// Return a [RepoKeysBuilder].
    pub fn builder() -> RepoKeysBuilder {
        RepoKeysBuilder::new()
    }

    /// Create a [RepoKeys] from a pm-style keys directory, which can optionally contain the
    /// following files:
    /// * root.json - all the root metadata private keys.
    /// * targets.json - all the targets metadata private keys.
    /// * snapshot.json - all the snapshot metadata private keys.
    /// * timestamp.json - all the timestamp metadata private keys.
    pub fn from_dir(path: &Path) -> Result<Self, ParseError> {
        Ok(RepoKeys {
            root_keys: parse_keys_if_exists(&path.join("root.json"))?,
            targets_keys: parse_keys_if_exists(&path.join("targets.json"))?,
            snapshot_keys: parse_keys_if_exists(&path.join("snapshot.json"))?,
            timestamp_keys: parse_keys_if_exists(&path.join("timestamp.json"))?,
        })
    }

    /// Return all the loaded [PrivateKey]s for the root metadata.
    pub fn root_keys(&self) -> &[Box<dyn PrivateKey>] {
        &self.root_keys
    }

    /// Return all the loaded [PrivateKey]s for the targets metadata.
    pub fn targets_keys(&self) -> &[Box<dyn PrivateKey>] {
        &self.targets_keys
    }

    /// Return all the loaded [PrivateKey]s for the snapshot metadata.
    pub fn snapshot_keys(&self) -> &[Box<dyn PrivateKey>] {
        &self.snapshot_keys
    }

    /// Return all the loaded [PrivateKey]s for the timestamp metadata.
    pub fn timestamp_keys(&self) -> &[Box<dyn PrivateKey>] {
        &self.timestamp_keys
    }
}

/// Helper to construct [RepoKeys].
#[derive(Debug)]
pub struct RepoKeysBuilder {
    keys: RepoKeys,
}

impl RepoKeysBuilder {
    /// Construct a new [RepoKeysBuilder].
    pub fn new() -> Self {
        RepoKeysBuilder {
            keys: RepoKeys {
                root_keys: vec![],
                targets_keys: vec![],
                snapshot_keys: vec![],
                timestamp_keys: vec![],
            },
        }
    }

    /// Add a [PrivateKey] that will be used as a root key.
    pub fn add_root_key(mut self, key: Box<dyn PrivateKey>) -> Self {
        self.keys.root_keys.push(key);
        self
    }

    /// Add a [PrivateKey] that will be used as a root key.
    pub fn add_targets_key(mut self, key: Box<dyn PrivateKey>) -> Self {
        self.keys.targets_keys.push(key);
        self
    }

    /// Add a [PrivateKey] that will be used as a root key.
    pub fn add_snapshot_key(mut self, key: Box<dyn PrivateKey>) -> Self {
        self.keys.snapshot_keys.push(key);
        self
    }

    /// Add a [PrivateKey] that will be used as a root key.
    pub fn add_timestamp_key(mut self, key: Box<dyn PrivateKey>) -> Self {
        self.keys.timestamp_keys.push(key);
        self
    }

    /// Load root metadata [PrivateKey]s from a pm-style json file.
    pub fn load_root_keys(mut self, path: &Path) -> Result<Self, ParseError> {
        self.keys.root_keys.extend(parse_keys(File::open(path)?)?);
        Ok(self)
    }

    /// Load targets metadata [PrivateKey]s from a pm-style json file.
    pub fn load_targets_keys(mut self, path: &Path) -> Result<Self, ParseError> {
        self.keys.targets_keys.extend(parse_keys(File::open(path)?)?);
        Ok(self)
    }

    /// Load snapshot metadata [PrivateKey]s from a pm-style json file.
    pub fn load_snapshot_keys(mut self, path: &Path) -> Result<Self, ParseError> {
        self.keys.snapshot_keys.extend(parse_keys(File::open(path)?)?);
        Ok(self)
    }

    /// Load timestamp metadata [PrivateKey]s from a pm-style json file.
    pub fn load_timestamp_keys(mut self, path: &Path) -> Result<Self, ParseError> {
        self.keys.timestamp_keys.extend(parse_keys(File::open(path)?)?);
        Ok(self)
    }

    pub fn build(self) -> RepoKeys {
        self.keys
    }
}

/// Try to open the key file. Return an empty vector if the file doesn't exist.
fn parse_keys_if_exists(path: &Path) -> Result<Vec<Box<dyn PrivateKey>>, ParseError> {
    match File::open(&path) {
        Ok(f) => parse_keys(f),
        Err(err) => {
            if err.kind() == io::ErrorKind::NotFound {
                Ok(vec![])
            } else {
                Err(err.into())
            }
        }
    }
}

/// Open the key file.
fn parse_keys(f: File) -> Result<Vec<Box<dyn PrivateKey>>, ParseError> {
    let role_keys: RoleKeys = serde_json::from_reader(f)?;

    let mut keys = Vec::with_capacity(role_keys.data.len());
    for RoleKey { keytype, scheme, keyid_hash_algorithms, keyval } in role_keys.data {
        match (keytype, scheme) {
            (KeyType::Ed25519, SignatureScheme::Ed25519) => {
                let (public, private) = if let Some(keyid_hash_algorithms) = keyid_hash_algorithms {
                    (
                        PublicKey::from_ed25519_with_keyid_hash_algorithms(
                            keyval.public,
                            Some(keyid_hash_algorithms.clone()),
                        )?,
                        Ed25519PrivateKey::from_ed25519_with_keyid_hash_algorithms(
                            &keyval.private,
                            Some(keyid_hash_algorithms),
                        )?,
                    )
                } else {
                    (
                        PublicKey::from_ed25519(keyval.public)?,
                        Ed25519PrivateKey::from_ed25519(&keyval.private)?,
                    )
                };

                if public.as_bytes() != private.public().as_bytes() {
                    return Err(ParseError::PublicKeyDoesNotMatchPrivateKey {
                        expected: private.public().clone(),
                        actual: public,
                    });
                }

                keys.push(Box::new(private) as Box<_>);
            }
            (keytype, scheme) => {
                return Err(ParseError::UnsupportedKeyTypeAndScheme { keytype, scheme });
            }
        };
    }

    Ok(keys)
}

#[derive(Deserialize)]
struct RoleKeys {
    data: Vec<RoleKey>,
}

#[derive(Deserialize)]
struct RoleKey {
    keytype: KeyType,
    scheme: SignatureScheme,
    /// The `keyid_hash_algorithms` is a deprecated field we added for support in order to test
    /// against python-tuf, which accidentally incorporated this field into the keyid computation.
    /// If the field is present, the value will be incorporated into the computation of the keyid,
    /// even if the field is empty. That's why it needs the option wrapper so we can distinguish
    /// between the value not being specified from it being empty.
    #[serde(default)]
    keyid_hash_algorithms: Option<Vec<String>>,
    keyval: KeyVal,
}

#[derive(Deserialize)]
struct KeyVal {
    #[serde(deserialize_with = "deserialize_hex")]
    public: Vec<u8>,
    #[serde(deserialize_with = "deserialize_hex")]
    private: Vec<u8>,
}

fn deserialize_hex<'de, D>(deserializer: D) -> Result<Vec<u8>, D::Error>
where
    D: Deserializer<'de>,
{
    let s = String::deserialize(deserializer)?;
    HEXLOWER.decode(s.as_bytes()).map_err(serde::de::Error::custom)
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::test_utils, assert_matches::assert_matches, camino::Utf8Path,
        serde_json::json, std::io::Write as _,
    };

    macro_rules! assert_keys {
        ($actual:expr, $expected:expr) => {
            assert_eq!(
                $actual.iter().map(|key| key.public()).collect::<Vec<_>>(),
                $expected.iter().collect::<Vec<_>>(),
            )
        };
    }

    #[test]
    fn test_parsing_empty_builder() {
        let keys = RepoKeys::builder().build();

        assert_keys!(keys.root_keys(), &[]);
        assert_keys!(keys.targets_keys(), &[]);
        assert_keys!(keys.snapshot_keys(), &[]);
        assert_keys!(keys.timestamp_keys(), &[]);
    }

    #[test]
    fn test_parsing_empty_file() {
        let json_keys = json!({
            "encrypted": false,
            "data": [],
        });
        let (file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        serde_json::to_writer(file, &json_keys).unwrap();

        let keys = RepoKeys::builder().load_root_keys(&temp_path).unwrap().build();

        assert_keys!(keys.root_keys(), &[]);
        assert_keys!(keys.targets_keys(), &[]);
        assert_keys!(keys.snapshot_keys(), &[]);
        assert_keys!(keys.timestamp_keys(), &[]);
    }

    #[test]
    fn test_parsing_keys() {
        let json_keys = json!({
            "encrypted": false,
            "data": [
                {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyid_hash_algorithms": [
                        "sha256"
                    ],
                    "keyval": {
                        "public":
                            "1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71",
                        "private":
                            "b841db733b03ee9f5061c3e5495545175e30cbfd167371bae0c5cf51f3065d8a\
                            1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71",
                    },
                },
                {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public":
                            "b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb",
                        "private":
                            "41a6dfefe5f29859014745a854bff4571b46c022714e3d1dfd6abdd67c10d750\
                            b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb",
                    },
                },
            ]
        });

        let (file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        serde_json::to_writer(file, &json_keys).unwrap();

        let keys = RepoKeys::builder().load_root_keys(&temp_path).unwrap().build();

        assert_keys!(
            keys.root_keys(),
            &[
                PublicKey::from_ed25519_with_keyid_hash_algorithms(
                    HEXLOWER
                        .decode(b"1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71")
                        .unwrap(),
                    Some(vec!["sha256".into()]),
                )
                .unwrap(),
                PublicKey::from_ed25519(
                    HEXLOWER
                        .decode(b"b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb")
                        .unwrap(),
                )
                .unwrap(),
            ]
        );
        assert_keys!(keys.targets_keys(), &[]);
        assert_keys!(keys.snapshot_keys(), &[]);
        assert_keys!(keys.timestamp_keys(), &[]);
    }

    #[test]
    fn test_from_dir_all_keys() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();
        test_utils::make_empty_pm_repo_dir(&dir);

        let keys = RepoKeys::from_dir(&dir.join("keys").into_std_path_buf()).unwrap();

        assert_keys!(
            keys.root_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
        assert_keys!(
            keys.targets_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"d18d96532e15f1f8e2e2307d23bbbfc4df90782273abcf642740642d8871a640")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
        assert_keys!(
            keys.snapshot_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
        assert_keys!(
            keys.timestamp_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"10dd7f1b17b379cbce30f09ffcb582b3c2bf7923b2bb99399966569867c4eeaa")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
    }

    #[test]
    fn test_from_dir_some_keys() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();
        test_utils::make_empty_pm_repo_dir(&dir);

        let keys_dir = dir.join("keys");
        std::fs::remove_file(&keys_dir.join("root.json")).unwrap();
        let keys = RepoKeys::from_dir(&keys_dir.into_std_path_buf()).unwrap();

        assert_keys!(keys.root_keys(), &[]);
        assert_keys!(
            keys.targets_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"d18d96532e15f1f8e2e2307d23bbbfc4df90782273abcf642740642d8871a640")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
        assert_keys!(
            keys.snapshot_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
        assert_keys!(
            keys.timestamp_keys(),
            &[PublicKey::from_ed25519_with_keyid_hash_algorithms(
                HEXLOWER
                    .decode(b"10dd7f1b17b379cbce30f09ffcb582b3c2bf7923b2bb99399966569867c4eeaa")
                    .unwrap(),
                Some(vec!["sha256".into()]),
            )
            .unwrap()]
        );
    }

    #[test]
    fn test_from_dir_empty() {
        let tmp = tempfile::tempdir().unwrap();
        let keys = RepoKeys::from_dir(tmp.path()).unwrap();

        assert_keys!(keys.root_keys(), &[]);
        assert_keys!(keys.targets_keys(), &[]);
        assert_keys!(keys.snapshot_keys(), &[]);
        assert_keys!(keys.timestamp_keys(), &[]);
    }

    #[test]
    fn test_parsing_keys_missing_file() {
        let tmp = tempfile::tempdir().unwrap();
        assert_matches!(
            RepoKeys::builder().load_root_keys(&tmp.path().join("does-not-exist")),
            Err(ParseError::Io(_))
        );
    }

    #[test]
    fn test_parsing_keys_malformed_json() {
        let (mut file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        file.write_all(b"invalid json").unwrap();
        drop(file);

        assert_matches!(RepoKeys::builder().load_root_keys(&temp_path), Err(ParseError::Json(_)));
    }

    #[test]
    fn test_parsing_keys_rejects_unknown_keytype() {
        let json_keys = json!({
            "encrypted": false,
            "data": [
                {
                    "keytype": "unknown",
                    "scheme": "ed25519",
                    "keyval": {
                        "public":
                            "b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb",
                        "private":
                            "b841db733b03ee9f5061c3e5495545175e30cbfd167371bae0c5cf51f3065d8a\
                            1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71",
                    },
                },
            ]
        });

        let (file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        serde_json::to_writer(file, &json_keys).unwrap();

        assert_matches!(
            RepoKeys::builder().load_root_keys(&temp_path),
            Err(ParseError::UnsupportedKeyTypeAndScheme { keytype, scheme })
            if keytype == KeyType::Unknown("unknown".into()) && scheme == SignatureScheme::Ed25519
        );
    }

    #[test]
    fn test_parsing_keys_rejects_unknown_scheme() {
        let json_keys = json!({
            "encrypted": false,
            "data": [
                {
                    "keytype": "ed25519",
                    "scheme": "unknown",
                    "keyval": {
                        "public":
                            "b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb",
                        "private":
                            "b841db733b03ee9f5061c3e5495545175e30cbfd167371bae0c5cf51f3065d8a\
                            1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71",
                    },
                },
            ]
        });

        let (file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        serde_json::to_writer(file, &json_keys).unwrap();

        assert_matches!(
            RepoKeys::builder().load_root_keys(&temp_path),
            Err(ParseError::UnsupportedKeyTypeAndScheme { keytype, scheme })
            if keytype == KeyType::Ed25519 && scheme == SignatureScheme::Unknown("unknown".into())
        );
    }

    #[test]
    fn test_parsing_keys_rejects_wrong_keys() {
        let json_keys = json!({
            "encrypted": false,
            "data": [
                {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public":
                            "b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb",
                        "private":
                            "b841db733b03ee9f5061c3e5495545175e30cbfd167371bae0c5cf51f3065d8a\
                            1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71",
                    },
                },
            ]
        });

        let (file, temp_path) = tempfile::NamedTempFile::new().unwrap().into_parts();
        serde_json::to_writer(file, &json_keys).unwrap();

        assert_matches!(
            RepoKeys::builder().load_root_keys(&temp_path),
            Err(ParseError::PublicKeyDoesNotMatchPrivateKey { expected, actual })
            if expected == PublicKey::from_ed25519(
                    HEXLOWER
                        .decode(b"1d4c564cb8466c49f97f042f5f3f242365ffd4210a9a1a82759d7d58afd66d71")
                        .unwrap(),
            ).unwrap() && actual ==
                PublicKey::from_ed25519(
                    HEXLOWER
                        .decode(b"b3ef3423402006eba0775f51f1fa4b38b70297098a0f40d699e984d76a6b83fb")
                        .unwrap(),
                ).unwrap()
        );
    }
}
