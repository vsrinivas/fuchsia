// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::prelude::DataCollection,
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    thiserror::Error,
    uuid::Uuid,
};

#[derive(Clone, Debug, Deserialize, Serialize, Error)]
#[serde(rename_all = "snake_case")]
pub enum DevmgrConfigError {
    #[error("Failed to open ZBI file from path {zbi_path}\n{io_error}")]
    FailedToOpenZbi { zbi_path: String, io_error: String },
    #[error("Failed to read ZBI file at {zbi_path}\n{io_error:#?}")]
    FailedToReadZbi { zbi_path: String, io_error: String },
    #[error("Failed to parse ZBI file at {zbi_path}\n{zbi_error}")]
    FailedToParseZbi { zbi_path: String, zbi_error: String },
    #[error("Failed to parse bootfs from ZBI file at {zbi_path}\n{bootfs_error}")]
    FailedToParseBootfs { zbi_path: String, bootfs_error: String },
    #[error("Failed to parse UTF8 string from devmgr config at bootfs:{devmgr_config_path} in ZBI at {zbi_path}\n{utf8_error}")]
    FailedToParseUtf8DevmgrConfig {
        zbi_path: String,
        devmgr_config_path: String,
        utf8_error: String,
    },
    #[error("Failed to parse devmgr config format from devmgr config at bootfs:{devmgr_config_path} in ZBI at {zbi_path}\n{parse_error}")]
    FailedToParseDevmgrConfigFormat {
        zbi_path: String,
        devmgr_config_path: String,
        parse_error: DevmgrConfigParseError,
    },
    #[error(
        "Failed to locate devmgr config file at bootfs:{devmgr_config_path} in ZBI at {zbi_path}"
    )]
    FailedToLocateDevmgrConfig { zbi_path: String, devmgr_config_path: String },
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
    pub deps: Vec<String>,
    pub devmgr_config: Option<DevmgrConfigContents>,
    pub errors: Vec<DevmgrConfigError>,
}

impl DataCollection for DevmgrConfigCollection {
    fn uuid() -> Uuid {
        Uuid::parse_str("0b2df114-3fab-465c-8ade-dd7ba6339961").unwrap()
    }
    fn collection_name() -> String {
        "Devmgr Config Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains [key] => [[values]] entries loaded from a devmgr config file".to_string()
    }
}
