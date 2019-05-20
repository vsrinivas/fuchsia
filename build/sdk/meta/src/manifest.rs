// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use serde_derive::{Deserialize, Serialize};

use crate::common::TargetArchitecture;
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Architectures {
    pub host: String,
    pub target: Vec<TargetArchitecture>,
}

#[derive(Serialize, Deserialize, Debug, Hash, PartialEq, Eq, Clone)]
#[serde(rename_all = "snake_case")]
pub enum ElementType {
    BanjoLibrary,
    CcPrebuiltLibrary,
    CcSourceLibrary,
    DartLibrary,
    Documentation,
    FidlLibrary,
    HostTool,
    Image,
    LoadableModule,
    Sysroot,
}

#[derive(Serialize, Deserialize, Debug, Hash, PartialEq, Eq, Clone)]
#[serde(deny_unknown_fields)]
pub struct Part {
    pub meta: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
}

impl fmt::Display for Part {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}[{:?}]", self.meta, self.kind)
    }
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub arch: Architectures,
    pub schema_version: String,
    pub id: String,
    pub parts: Vec<Part>,
}

impl JsonObject for Manifest {
    fn get_schema() -> &'static str {
        include_str!("../manifest.json")
    }
}

#[cfg(test)]
mod tests {
    use crate::json::JsonObject;

    use super::Manifest;

    #[test]
    /// Verifies that the Manifest class matches its schema.
    /// This is a quick smoke test to ensure the class and its schema remain in sync.
    fn test_validation() {
        let data = r#"
        {
            "arch": {
                "host": "x86_128-fuchsia",
                "target": [
                    "x64"
                ]
            },
            "parts": [],
            "id": "foobarblah",
            "schema_version": "314"
        }"#;
        let manifest = Manifest::new(data.as_bytes()).unwrap();
        manifest.validate().unwrap();
    }
}
