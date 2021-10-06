// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::groups::ArtifactStoreType,
    crate::spec::SpecArtifactStoreKind,
    serde::{Deserialize, Serialize},
    serde_json::{Map, Value},
};

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct Lock {
    pub artifacts: Vec<LockArtifact>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct LockArtifact {
    pub name: String,
    pub r#type: ArtifactStoreType,
    pub artifact_store: LockArtifactStore,
    pub attributes: Map<String, Value>,
    pub merkle: String,
    pub blobs: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct LockArtifactStore {
    pub name: String,
    pub r#type: SpecArtifactStoreKind,
    pub repo: Option<String>,
    pub artifact_group_name: String,
    pub content_address_storage: Option<String>,
}
