// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::prelude::DataCollection,
    scrutiny_utils::package::{deserialize_pkg_index, serialize_pkg_index, PackageIndexContents},
    serde::{Deserialize, Serialize},
    std::path::PathBuf,
    thiserror::Error,
};

/// Error that may occur when reading the ZBI.
#[derive(Clone, Debug, Deserialize, Serialize, Error)]
#[serde(rename_all = "snake_case")]
pub enum ZbiError {
    #[error("Failed to open ZBI from update package at {update_package_path}\n{io_error}")]
    FailedToOpenUpdatePackage { update_package_path: PathBuf, io_error: String },
    #[error("Failed to read ZBI from update package at {update_package_path}\n{io_error}")]
    FailedToReadZbi { update_package_path: PathBuf, io_error: String },
    #[error("Failed to parse ZBI from update package at {update_package_path}\n{zbi_error}")]
    FailedToParseZbi { update_package_path: PathBuf, zbi_error: String },
    #[error("Failed to parse bootfs from ZBI from update package at {update_package_path}\n{bootfs_error}")]
    FailedToParseBootfs { update_package_path: PathBuf, bootfs_error: String },
}

/// The collected bootfs artifacts from reading the ZBI.
#[derive(Deserialize, Serialize)]
pub struct BootFsCollection {
    pub files: Vec<String>,
    #[serde(serialize_with = "serialize_pkg_index", deserialize_with = "deserialize_pkg_index")]
    pub packages: Option<PackageIndexContents>,
}

impl DataCollection for BootFsCollection {
    fn collection_name() -> String {
        "BootFS Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains bootfs files loaded from a ZBI".to_string()
    }
}

/// The collected kernel cmdline arguments from reading the ZBI.
#[derive(Deserialize, Serialize)]
pub struct CmdlineCollection {
    pub cmdline: Vec<String>,
}

impl DataCollection for CmdlineCollection {
    fn collection_name() -> String {
        "Kernel Cmdline Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains the kernel cmdline loaded from a ZBI".to_string()
    }
}
