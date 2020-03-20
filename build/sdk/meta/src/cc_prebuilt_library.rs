// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

use crate::common::{CcLibraryName, ElementType, File, TargetArchitecture};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "lowercase")]
pub enum Format {
    Shared,
    Static,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct BinaryGroup {
    pub link: File,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dist: Option<File>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dist_path: Option<File>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub debug: Option<File>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(deny_unknown_fields)]
pub struct CcPrebuiltLibrary {
    pub name: CcLibraryName,
    pub format: Format,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub headers: Vec<File>,
    pub include_dir: File,
    pub deps: Vec<CcLibraryName>,
    pub binaries: HashMap<TargetArchitecture, BinaryGroup>,
}

impl JsonObject for CcPrebuiltLibrary {
    fn get_schema() -> &'static str {
        include_str!("../cc_prebuilt_library.json")
    }
}

#[cfg(test)]
mod tests {
    use super::CcPrebuiltLibrary;

    test_validation! {
        name = test_validation,
        kind = CcPrebuiltLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "cc_prebuilt_library",
            "format": "shared",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "headers": [
                "pkg/foobar/include/one.h",
                "pkg/foobar/include/two.h"
            ],
            "include_dir": "pkg/foobar/include",
            "binaries": {
                "x64": {
                    "link": "arch/x64/lib/libfoobar.so",
                    "dist": "arch/x64/dist/libfoobar.so",
                    "dist_path": "lib/libfoobar.so"
                },
                "arm64": {
                    "link": "arch/arm64/lib/libfoobar.so"
                }
            }
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = CcPrebuiltLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "cc_prebuilt_library",
            "format": "shared",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "headers": [
                "pkg/foobar/include/one.h",
                "pkg/foobar/include/two.h"
            ],
            "include_dir": "pkg/foobar/include",
            "binaries": {}
        }
        "#,
        // Binaries are empty.
        valid = false,
    }
}
