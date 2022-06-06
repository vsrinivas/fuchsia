// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::devmgr_config::DevmgrConfigError,
    fuchsia_merkle::Hash,
    fuchsia_url::{PackageName, PackageVariant},
    scrutiny::prelude::DataCollection,
    serde::{
        de::{self, Deserializer, Error as _, MapAccess, Visitor},
        ser::Serializer,
        Deserialize, Serialize,
    },
    std::{
        collections::{HashMap, HashSet},
        fmt,
        path::PathBuf,
        str::FromStr,
    },
    thiserror::Error,
    uuid::Uuid,
};

#[derive(Debug, Deserialize, Serialize, Error)]
#[serde(rename_all = "snake_case")]
pub enum StaticPkgsError {
    #[error("Failed to construct blobfs artifact reader with build path {build_path} and blobfs archives {blobfs_paths:?} for static packages collector: {blobfs_error}")]
    FailedToLoadBlobfs { build_path: PathBuf, blobfs_paths: Vec<PathBuf>, blobfs_error: String },
    #[error("Failed to read ZBI devmgr config data from model: {model_error}")]
    FailedToReadDevmgrConfigData { model_error: String },
    #[error("Data model does not contain ZBI devmgr config data")]
    MissingDevmgrConfigData,
    #[error("Data model for devmgr config data contains errors")]
    DevmgrConfigDataContainsErrors { devmgr_config_data_errors: Vec<DevmgrConfigError> },
    #[error("devmgr config is missing the pkgfs cmd entry")]
    MissingPkgfsCmdEntry,
    #[error("Unexpected number of pkgfs cmd entry arguments: expected {expected_len}; actual: {actual_len}")]
    UnexpectedPkgfsCmdLen { expected_len: usize, actual_len: usize },
    #[error("Unexpected pkgfs command: expected {expected_cmd}; actual {actual_cmd}")]
    UnexpectedPkgfsCmd { expected_cmd: String, actual_cmd: String },
    #[error("Malformed system image hash: expected hex-SHA256; actual {actual_hash}")]
    MalformedSystemImageHash { actual_hash: String },
    #[error("Failed to open system image file: {system_image_path}: {io_error}")]
    FailedToOpenSystemImage { system_image_path: PathBuf, io_error: String },
    #[error("Failed to read system image file: {system_image_path}: {io_error}")]
    FailedToReadSystemImage { system_image_path: PathBuf, io_error: String },
    #[error("Failed to verify system image file: expected merkle root: {expected_merkle_root}; computed merkle root: {computed_merkle_root}")]
    FailedToVerifySystemImage { expected_merkle_root: Hash, computed_merkle_root: Hash },
    #[error("Failed to parse system image file: {system_image_path}: {parse_error}")]
    FailedToParseSystemImage { system_image_path: PathBuf, parse_error: String },
    #[error("Failed to read file, {file_name}, from system image file: {system_image_path}: {far_error}")]
    FailedToReadSystemImageMetaFile {
        system_image_path: PathBuf,
        file_name: String,
        far_error: String,
    },
    #[error("Failed to decode file, {file_name}, from system image file: {system_image_path}: {utf8_error}")]
    FailedToDecodeSystemImageMetaFile {
        system_image_path: PathBuf,
        file_name: String,
        utf8_error: String,
    },
    #[error("Failed to parse file, {file_name}, from system image file: {system_image_path}: {parse_error}")]
    FailedToParseSystemImageMetaFile {
        system_image_path: PathBuf,
        file_name: String,
        parse_error: String,
    },
    #[error(
        "Missing static packages entry in {file_name} from system image file: {system_image_path}"
    )]
    MissingStaticPkgsEntry { system_image_path: PathBuf, file_name: String },
    #[error("Malformed static packages hash: expected hex-SHA256; actual {actual_hash}")]
    MalformedStaticPkgsHash { actual_hash: String },
    #[error("Failed to read static packages file: {static_pkgs_path}: {io_error}")]
    FailedToReadStaticPkgs { static_pkgs_path: PathBuf, io_error: String },
    #[error("Failed to verify static packages file: expected merkle root: {expected_merkle_root}; computed merkle root: {computed_merkle_root}")]
    FailedToVerifyStaticPkgs { expected_merkle_root: Hash, computed_merkle_root: Hash },
    #[error("Failed to parse static packages file: {static_pkgs_path}: {parse_error}")]
    FailedToParseStaticPkgs { static_pkgs_path: PathBuf, parse_error: String },
}

/// Static packages file contains lines of the form:
/// [pkg-name-variant-path]=[merkle-root-hash].
pub type StaticPkgsContents = HashMap<(PackageName, Option<PackageVariant>), Hash>;

#[derive(Deserialize, Serialize)]
pub struct StaticPkgsCollection {
    pub deps: HashSet<PathBuf>,
    #[serde(
        serialize_with = "serialize_static_pkgs",
        deserialize_with = "deserialize_static_pkgs"
    )]
    pub static_pkgs: Option<StaticPkgsContents>,
    pub errors: Vec<StaticPkgsError>,
}

/// Serialize static packages listing contents. A custom strategy is necessary because
/// map keys are stored as `(PackageName, Option<PackageVariant>)`, which must be manually converted
/// to a string representation.
pub fn serialize_static_pkgs<S>(
    static_pkgs: &Option<StaticPkgsContents>,
    serializer: S,
) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    match static_pkgs {
        None => serializer.serialize_none(),
        Some(static_pkgs) => {
            let mut map = HashMap::new();
            for ((name, variant), hash) in static_pkgs {
                match variant {
                    None => {
                        map.insert(name.to_string(), hash.to_string());
                    }
                    Some(variant) => {
                        map.insert(
                            format!("{}/{}", name.as_ref(), variant.as_ref()),
                            hash.to_string(),
                        );
                    }
                }
            }
            serializer.serialize_some(&map)
        }
    }
}

/// Deserialize static packages listing contents. A custom strategy is necessary because
/// map keys are stored as `(PackageName, Option<PackageVariant>)`, which must be manually converted
/// from a string representation.
pub fn deserialize_static_pkgs<'de, D>(
    deserializer: D,
) -> Result<Option<StaticPkgsContents>, D::Error>
where
    D: Deserializer<'de>,
{
    struct OptVisitor;
    struct MapVisitor;

    impl<'de> Visitor<'de> for OptVisitor {
        type Value = Option<StaticPkgsContents>;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("optional static_pkgs map")
        }

        fn visit_none<E>(self) -> Result<Self::Value, E>
        where
            E: de::Error,
        {
            Ok(None)
        }

        fn visit_some<D>(self, deserializer: D) -> Result<Self::Value, D::Error>
        where
            D: Deserializer<'de>,
        {
            let visitor = MapVisitor;
            Ok(Some(deserializer.deserialize_any(visitor)?))
        }
    }

    impl<'de> Visitor<'de> for MapVisitor {
        type Value = StaticPkgsContents;

        fn expecting(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            formatter.write_str("static_pkgs map")
        }

        fn visit_map<A>(self, mut map_access: A) -> Result<Self::Value, A::Error>
        where
            A: MapAccess<'de>,
        {
            let mut map = HashMap::with_capacity(map_access.size_hint().unwrap_or(0));
            loop {
                let entry: Option<(String, String)> = map_access.next_entry()?;
                match entry {
                    None => break,
                    Some((name_variant_string, hash_string)) => {
                        let mut name_parts = vec![];
                        let mut variant_part = None;
                        for part in name_variant_string.split("/") {
                            if let Some(prev_tail) = variant_part {
                                name_parts.push(prev_tail);
                            }
                            variant_part = Some(part);
                        }
                        let name_string = name_parts.join("/");
                        let name = PackageName::from_str(&name_string).map_err(|err| {
                            A::Error::custom(&format!(
                                "Failed to parse package name from string: {}: {}",
                                name_string, err
                            ))
                        })?;
                        let variant = variant_part
                            .map(|variant| {
                                PackageVariant::from_str(variant).map_err(|err| {
                                    A::Error::custom(&format!(
                                        "Failed to parse package variant from string: {}: {}",
                                        variant, err
                                    ))
                                })
                            })
                            .map_or(Ok(None), |r| r.map(Some))?;
                        let hash = Hash::from_str(&hash_string).map_err(|err| {
                            A::Error::custom(&format!(
                                "Failed to parse package hash from string: {}: {}",
                                hash_string, err
                            ))
                        })?;
                        map.insert((name, variant), hash);
                    }
                }
            }
            Ok(map)
        }
    }

    let visitor = OptVisitor;
    deserializer.deserialize_option(visitor)
}

impl DataCollection for StaticPkgsCollection {
    fn uuid() -> Uuid {
        Uuid::parse_str("b55d0f7f-b776-496c-83a3-63a6745a3a71").unwrap()
    }
    fn collection_name() -> String {
        "Static packages list".to_string()
    }
    fn collection_description() -> String {
        "Contains [path] => [hash] entries loaded from a static packages manifest file".to_string()
    }
}
