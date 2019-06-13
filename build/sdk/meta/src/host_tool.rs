// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde_derive::{Deserialize, Serialize};

use crate::common::{ElementType, File, TargetArchitecture};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct HostTool {
    pub name: String,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub files: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub target_files: Option<HashMap<TargetArchitecture, Vec<String>>>,
}

impl JsonObject for HostTool {
    fn get_schema() -> &'static str {
        include_str!("../host_tool.json")
    }
}

#[cfg(test)]
mod tests {
    use crate::json::JsonObject;

    use super::HostTool;

    #[test]
    /// Verifies that the HostTool class matches its schema.
    /// This is a quick smoke test to ensure the class and its schema remain in sync.
    fn test_validation() {
        let data = r#"
        {
            "name": "foobar",
            "type": "host_tool",
            "root": "tools/foobar",
            "files": [
                "tools/foobar/one",
                "tools/foobar/two"
            ],
            "target_files": {
                "x64": [
                    "tools/foobar/foobar_x64"
                ]
            }
        }
        "#;
        let tool = HostTool::new(data.as_bytes()).unwrap();
        tool.validate().unwrap();
    }

    #[test]
    fn test_validation_invalid() {
        // This data has the wrong type.
        let data = r#"
        {
            "name": "foobar",
            "type": "cc_prebuilt_library",
            "root": "tools/foobar",
            "files": [
                "tools/foobar/one",
                "tools/foobar/two"
            ],
            "target_files": {
                "x64": [
                    "tools/foobar/foobar_x64"
                ]
            }
        }
        "#;
        let tool = HostTool::new(data.as_bytes()).unwrap();
        assert!(tool.validate().is_err(), "Validation should have failed.");
    }
}
