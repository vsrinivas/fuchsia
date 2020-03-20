// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::common::{ElementType, File, TargetArchitecture};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct LoadableModule {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub resources: Vec<File>,
    pub binaries: HashMap<TargetArchitecture, Vec<File>>,
    pub root: File,
}

impl JsonObject for LoadableModule {
    fn get_schema() -> &'static str {
        include_str!("../loadable_module.json")
    }
}

#[cfg(test)]
mod tests {
    use super::LoadableModule;

    test_validation! {
        name = test_validation,
        kind = LoadableModule,
        data = r#"
        {
            "name": "foobar",
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.one",
                "pkg/foobar/res.two"
            ],
            "binaries": {
                "x64": [
                    "arch/x64/lib/foobar.stg"
                ],
                "arm64": [
                    "arch/arm64/lib/foobar.stg"
                ]
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = LoadableModule,
        data = r#"
        {
            "name": "foobar",
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.one",
                "pkg/foobar/res.two"
            ],
            "binaries": {}
        }
        "#,
        // Binaries are empty.
        valid = false,
    }
}
