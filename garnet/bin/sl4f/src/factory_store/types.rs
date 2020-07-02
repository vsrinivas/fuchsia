// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

/// Supported factory commands.
pub enum FactoryStoreMethod {
    ListFiles,
    ReadFile,
}

impl std::str::FromStr for FactoryStoreMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "ListFiles" => Ok(FactoryStoreMethod::ListFiles),
            "ReadFile" => Ok(FactoryStoreMethod::ReadFile),
            _ => return Err(format_err!("invalid Factory FIDL method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct ListFilesRequest {
    pub provider: FactoryStoreProvider,
}

#[derive(Deserialize, Debug)]
pub struct ReadFileRequest {
    pub provider: FactoryStoreProvider,
    pub filename: String,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub enum FactoryStoreProvider {
    Alpha,
    Cast,
    Misc,
    Playready,
    Weave,
    Widevine,
}
