// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    serde::{Deserialize, Serialize},
    serde_json::{Map, Value},
};
// The specification file format

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct Spec {
    pub spec_version: Option<String>,
    pub product: String,
    pub attributes: Option<Map<String, Value>>,
    pub constraints: Option<Vec<Constraint>>,
    pub artifact_groups: Vec<SpecArtifactGroup>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct SpecArtifactGroup {
    pub artifact_store: SpecArtifactStore,
    pub name: Option<String>,
    pub attributes: Option<Map<String, Value>>,
    pub artifacts: Vec<SpecArtifact>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct SpecArtifactStore {
    pub name: String,
    pub r#type: SpecArtifactStoreKind,
    // other fields
    pub path: Option<String>,
    pub repo: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct Constraint {
    pub equal: Vec<ConstraintEqual>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ConstraintEqual {
    pub artifact_group: String,
    pub artifact: String,
    pub attribute: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct SpecArtifact {
    pub name: String,
    pub attribtues: Option<Map<String, Value>>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub enum SpecArtifactStoreKind {
    #[serde(rename = "tuf")]
    TUF,
    #[serde(rename = "local")]
    Local,
}
