// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::{BanjoLibraryName, ElementType, File};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct BanjoLibrary {
    pub name: BanjoLibraryName,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub sources: Vec<File>,
    pub deps: Vec<BanjoLibraryName>,
}

impl JsonObject for BanjoLibrary {
    fn get_schema() -> &'static str {
        include_str!("../banjo_library.json")
    }
}

#[cfg(test)]
mod tests {
    use super::BanjoLibrary;

    test_validation! {
        name = test_validation,
        kind = BanjoLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "banjo_library",
            "root": "banjo/foo.bar",
            "deps": [
                "rab.oof"
            ],
            "sources": [
                "banjo/foo.bar/one.banjo",
                "banjo/foo.bar/two.banjo"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = BanjoLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "banjo_library",
            "root": "banjo/foo.bar",
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
