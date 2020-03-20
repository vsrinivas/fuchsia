// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::{ElementType, FidlLibraryName, File};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct FidlLibrary {
    pub name: FidlLibraryName,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub sources: Vec<File>,
    pub deps: Vec<FidlLibraryName>,
}

impl JsonObject for FidlLibrary {
    fn get_schema() -> &'static str {
        include_str!("../fidl_library.json")
    }
}

#[cfg(test)]
mod tests {
    use super::FidlLibrary;

    test_validation! {
        name = test_validation,
        kind = FidlLibrary,
        data = r#"
        {
            "name": "foo.bar",
            "type": "fidl_library",
            "root": "fidl/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "fidl/foo.bar/one.fidl",
                "fidl/foo.bar/two.fidl"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = FidlLibrary,
        data = r#"
        {
            "name": "foo.bar",
            "type": "fidl_library",
            "root": "fidl/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": []
        }
        "#,
        // Sources are empty.
        valid = false,
    }
}
