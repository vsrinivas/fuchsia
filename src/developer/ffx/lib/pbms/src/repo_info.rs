// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tracking information for a repository mirror.

use {
    anyhow::Result,
    serde::{Deserialize, Serialize},
    serde_json,
    std::{fs::File, path::Path},
};

#[derive(Debug, Default, Deserialize, Serialize)]
pub struct RepoInfo {
    pub metadata_url: String,
}

impl RepoInfo {
    /// Create a new RepoInfo by reading data from `path`.
    pub fn load(path: &Path) -> Result<Self> {
        let file = File::open(path)?;
        let result = serde_json::from_reader(file)?;
        Ok(result)
    }

    /// Write the RepoInfo data to `path`.
    ///
    /// Non-consuming: may be edit and saved again.
    pub fn save(&self, path: &Path) -> Result<()> {
        let file = File::create(path)?;
        serde_json::to_writer(file, &self)?;
        Ok(())
    }
}
