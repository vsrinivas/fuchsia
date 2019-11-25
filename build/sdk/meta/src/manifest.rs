// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;

use serde_derive::{Deserialize, Serialize};

use crate::common::{ElementType, TargetArchitecture};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(deny_unknown_fields)]
pub struct Architectures {
    pub host: String,
    pub target: Vec<TargetArchitecture>,
}

#[derive(Serialize, Deserialize, Debug, Hash, PartialEq, Eq, Clone, PartialOrd, Ord)]
#[serde(deny_unknown_fields)]
pub struct Part {
    pub meta: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
}

impl fmt::Display for Part {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
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
    use super::Manifest;

    test_validation! {
        name = test_validation,
        kind = Manifest,
        data = r#"
        {
            "arch": {
                "host": "x86_128-fuchsia",
                "target": [
                    "x64"
                ]
            },
            "parts": [
                {
                    "meta": "pkg/foo/meta.json",
                    "type": "cc_source_library"
                }
            ],
            "id": "foobarblah",
            "schema_version": "314"
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Manifest,
        data = r#"
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
        }
        "#,
        // Parts are empty.
        valid = false,
    }
}
