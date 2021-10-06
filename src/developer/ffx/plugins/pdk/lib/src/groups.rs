// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO - switch dependency
    serde::{Deserialize, Serialize},
    serde_json::Value,
};

// The artifact_groups.json file format for a artifact store

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStore {
    pub schema_version: String,
    pub artifact_groups: Vec<ArtifactStoreGroup>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStoreGroup {
    // Value preferred over Map to allow JSON Pointer
    pub attributes: Value,
    pub name: String,
    pub content_address_storage: Option<String>,
    pub artifacts: Vec<ArtifactStoreEntry>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStoreEntry {
    #[serde(rename = "merkle")]
    pub hash: String,
    pub sha256: String,
    pub name: String,
    pub r#type: ArtifactStoreType,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub enum ArtifactStoreType {
    #[serde(rename = "package")]
    Package,
}
