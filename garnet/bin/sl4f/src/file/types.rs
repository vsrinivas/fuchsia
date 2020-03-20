// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use serde::{Deserialize, Serialize};

/// Enum for supported file related commands.
pub enum FileMethod {
    DeleteFile,
    MakeDir,
    ReadFile,
    WriteFile,
    Stat,
}

impl std::str::FromStr for FileMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "DeleteFile" => Ok(FileMethod::DeleteFile),
            "MakeDir" => Ok(FileMethod::MakeDir),
            "ReadFile" => Ok(FileMethod::ReadFile),
            "WriteFile" => Ok(FileMethod::WriteFile),
            "Stat" => Ok(FileMethod::Stat),
            _ => return Err(format_err!("Invalid File Facade method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum DeleteFileResult {
    NotFound,
    Success,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum MakeDirResult {
    AlreadyExists,
    Success,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum WriteFileResult {
    Success,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
pub enum StatResult {
    NotFound,
    Success(Metadata),
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
pub struct Metadata {
    pub kind: NodeKind,
    pub size: u64,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum NodeKind {
    Directory,
    File,
    Other,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, to_value};

    #[test]
    fn serialize_stat_result_not_found() {
        assert_eq!(to_value(StatResult::NotFound).unwrap(), json!("NotFound"));
    }

    #[test]
    fn serialize_stat_result_success() {
        assert_eq!(
            to_value(StatResult::Success(Metadata { kind: NodeKind::File, size: 42 })).unwrap(),
            json!({
                "Success": {
                    "kind": "file",
                    "size": 42,
                },
            })
        );
    }
}
