// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO - switch dependency
    ffx_pdk_lib::lock::ArtifactType,
    serde::{Deserialize, Serialize},
    serde_json::Value,
};

// The artifact_groups.json file format for a artifact store

#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
pub struct ArtifactStoreGroups {
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
    pub r#type: ArtifactType,
}

// tests
#[cfg(test)]
mod test {
    use {super::*, serde_json};

    /// Test parsing a generic artifact_groups
    #[test]
    fn test_groups() {
        let data = include_str!("../test_data/artifact_groups.json");
        let v: ArtifactStoreGroups = serde_json::from_str(data).unwrap();
        assert_eq!(v.schema_version, "v1");
    }
}
