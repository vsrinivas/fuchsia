// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use serde_derive::{Deserialize, Serialize};

/// Enum for supported file related commands.
pub enum FileMethod {
    DeleteFile,
    MakeDir,
    ReadFile,
    WriteFile,
}

impl std::str::FromStr for FileMethod {
    type Err = failure::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "DeleteFile" => Ok(FileMethod::DeleteFile),
            "MakeDir" => Ok(FileMethod::MakeDir),
            "ReadFile" => Ok(FileMethod::ReadFile),
            "WriteFile" => Ok(FileMethod::WriteFile),
            _ => bail!("Invalid File Facade method: {}", method),
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
