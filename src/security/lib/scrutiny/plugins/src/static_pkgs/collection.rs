// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::devmgr_config::DevmgrConfigError,
    fuchsia_merkle::Hash,
    scrutiny::prelude::DataCollection,
    scrutiny_utils::package::{deserialize_pkg_index, serialize_pkg_index, PackageIndexContents},
    serde::{Deserialize, Serialize},
    std::{collections::HashSet, path::PathBuf},
    thiserror::Error,
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

#[derive(Deserialize, Serialize)]
pub struct StaticPkgsCollection {
    pub deps: HashSet<PathBuf>,
    #[serde(serialize_with = "serialize_pkg_index", deserialize_with = "deserialize_pkg_index")]
    pub static_pkgs: Option<PackageIndexContents>,
    pub errors: Vec<StaticPkgsError>,
}

impl DataCollection for StaticPkgsCollection {
    fn collection_name() -> String {
        "Static packages list".to_string()
    }
    fn collection_description() -> String {
        "Contains [path] => [hash] entries loaded from a static packages manifest file".to_string()
    }
}
