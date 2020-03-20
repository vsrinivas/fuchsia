// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::common::{ElementType, File, TargetArchitecture};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Version {
    pub root: File,
    pub headers: Vec<File>,
    pub dist_dir: File,
    pub include_dir: File,
    pub link_libs: Vec<File>,
    pub dist_libs: Vec<File>,
    pub debug_libs: Vec<File>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct Sysroot {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub versions: HashMap<TargetArchitecture, Version>,
}

impl JsonObject for Sysroot {
    fn get_schema() -> &'static str {
        include_str!("../sysroot.json")
    }
}

#[cfg(test)]
mod tests {
    use super::Sysroot;

    test_validation! {
        name = test_validation,
        kind = Sysroot,
        data = r#"
        {
            "name": "sysroot",
            "type": "sysroot",
            "versions": {
                "x64": {
                    "root": "arch/x64/sysroot",
                    "include_dir": "arch/x64/sysroot/include/",
                    "headers": [
                        "arch/x64/sysroot/include/foo.h"
                    ],
                    "dist_dir": "arch/x64/sysroot/dist",
                    "dist_libs": [
                        "arch/x64/sysroot/dist/one.so",
                        "arch/x64/sysroot/dist/two.so"
                    ],
                    "link_libs": [
                        "arch/x64/sysroot/lib/one.so",
                        "arch/x64/sysroot/lib/two.so"
                    ],
                    "debug_libs": [
                        ".build-id/aa/bb.so",
                        ".build-id/cc/dd.so"
                    ]
                }
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Sysroot,
        data = r#"
        {
            "name": "bogus",
            "type": "sysroot",
            "versions": {}
        }
        "#,
        // Name is wrong, versions are empty.
        valid = false,
    }
}
