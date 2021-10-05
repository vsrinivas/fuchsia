// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    ffx_pdk_lib::lock::ArtifactStoreType,
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
    pub artifact_store: ArtifactStore,
    pub name: Option<String>,
    pub attributes: Option<Map<String, Value>>,
    pub artifacts: Vec<Artifact>,
}

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStore {
    pub name: String,
    pub r#type: ArtifactStoreType,
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
pub struct Artifact {
    pub name: String,
    pub attribtues: Option<Map<String, Value>>,
}

// tests

#[cfg(test)]
mod test {
    use {super::*, serde_json5};

    /// Test parsing a generic spec
    #[test]
    fn test_spec() {
        let data = include_str!("../test_data/artifact_spec.json");
        let v: Spec = serde_json5::from_str(data).unwrap();
        assert_eq!(v.product, "workstation");
    }
}
