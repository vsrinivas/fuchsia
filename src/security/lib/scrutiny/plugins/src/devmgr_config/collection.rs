// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::prelude::DataCollection,
    serde::{Deserialize, Serialize},
    std::{
        collections::{HashMap, HashSet},
        path::PathBuf,
    },
    thiserror::Error,
};

#[derive(Clone, Debug, Deserialize, Serialize, Error)]
#[serde(rename_all = "snake_case")]
pub enum DevmgrConfigError {
    #[error("Failed to open blobfs using build path {build_path} and blobfs archive paths {blobfs_paths:?}\n{blobfs_error}")]
    FailedToOpenBlobfs { build_path: PathBuf, blobfs_paths: Vec<PathBuf>, blobfs_error: String },
    #[error("Failed to parse zbi config path {devmgr_config_path}")]
    FailedToParseDevmgrConfigPath { devmgr_config_path: PathBuf },
    #[error("Failed to open ZBI from update package at {update_package_path}\n{io_error}")]
    FailedToOpenUpdatePackage { update_package_path: PathBuf, io_error: String },
    #[error("Failed to read ZBI from update package at {update_package_path}\n{io_error}")]
    FailedToReadZbi { update_package_path: PathBuf, io_error: String },
    #[error("Failed to parse ZBI from update package at {update_package_path}\n{zbi_error}")]
    FailedToParseZbi { update_package_path: PathBuf, zbi_error: String },
    #[error("Failed to parse bootfs from ZBI from update package at {update_package_path}\n{bootfs_error}")]
    FailedToParseBootfs { update_package_path: PathBuf, bootfs_error: String },
    #[error("Failed to parse UTF8 string from devmgr config at bootfs:{devmgr_config_path} in ZBI from update package at {update_package_path}\n{utf8_error}")]
    FailedToParseUtf8DevmgrConfig {
        update_package_path: PathBuf,
        devmgr_config_path: PathBuf,
        utf8_error: String,
    },
    #[error("Failed to parse devmgr config format from devmgr config at bootfs:{devmgr_config_path} in ZBI from update package at {update_package_path}\n{parse_error}")]
    FailedToParseDevmgrConfigFormat {
        update_package_path: PathBuf,
        devmgr_config_path: PathBuf,
        parse_error: DevmgrConfigParseError,
    },
    #[error(
        "Failed to locate devmgr config file at bootfs:{devmgr_config_path} in ZBI from update package at {update_package_path}"
    )]
    FailedToLocateDevmgrConfig { update_package_path: PathBuf, devmgr_config_path: PathBuf },
}

#[derive(Clone, Debug, Deserialize, Serialize, Error)]
#[serde(rename_all = "snake_case")]
pub enum DevmgrConfigParseError {
    #[error("Failed to parse [unique-key]=[values] from devmgr config on line {line_no}:\n{line_contents}")]
    FailedToParseKeyValue { line_no: usize, line_contents: String },
    #[error("Devmgr config contains repeated key in [unique-key]=[values] on line {line_no}:\n{line_contents}\nPreviously declared on line {previous_line_no}:\n{previous_line_contents}")]
    RepeatedKey {
        line_no: usize,
        line_contents: String,
        previous_line_no: usize,
        previous_line_contents: String,
    },
}

/// Devmgr config file contains lines of the form:
/// [unique-key]=[[value-1]+[value-2]+[...]+[value-n]].
pub type DevmgrConfigContents = HashMap<String, Vec<String>>;

#[derive(Deserialize, Serialize)]
pub struct DevmgrConfigCollection {
    pub deps: HashSet<PathBuf>,
    pub devmgr_config: Option<DevmgrConfigContents>,
    pub errors: Vec<DevmgrConfigError>,
}

impl DataCollection for DevmgrConfigCollection {
    fn collection_name() -> String {
        "Devmgr Config Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains [key] => [[values]] entries loaded from a devmgr config file".to_string()
    }
}
