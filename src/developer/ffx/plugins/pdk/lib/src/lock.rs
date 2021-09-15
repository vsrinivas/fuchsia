// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactLock {
    pub artifacts: Vec<Artifact>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct Artifact {
    pub name: String,
    pub r#type: ArtifactType,
    pub artifact_store: ArtifactStore,
    pub attributes: BTreeMap<String, Value>,
    pub merkle: String,
    pub blobs: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStore {
    pub name: String,
    pub r#type: ArtifactStoreType,
    pub repo: String,
    pub artifact_group_name: String,
    pub content_address_storage: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub enum ArtifactType {
    #[serde(rename = "package")]
    Package,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub enum ArtifactStoreType {
    #[serde(rename = "tuf")]
    TUF,
    #[serde(rename = "local")]
    Local,
}
