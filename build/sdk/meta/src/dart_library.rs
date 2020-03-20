// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::{ElementType, FidlLibraryName, File};
use crate::json::JsonObject;

pub type PackageName = String;

#[derive(Serialize, Deserialize, Debug, Default, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct ThirdPartyLibrary {
    pub name: PackageName,
    pub version: String,
}

#[derive(Serialize, Deserialize, Debug, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct DartLibrary {
    pub name: PackageName,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub sources: Vec<File>,
    pub deps: Vec<PackageName>,
    pub fidl_deps: Vec<FidlLibraryName>,
    pub third_party_deps: Vec<ThirdPartyLibrary>,
}

impl JsonObject for DartLibrary {
    fn get_schema() -> &'static str {
        include_str!("../dart_library.json")
    }
}

#[cfg(test)]
mod tests {
    use super::DartLibrary;

    test_validation! {
        name = test_validation,
        kind = DartLibrary,
        data = r#"
        {
            "name": "fuchsia_foobar",
            "type": "dart_library",
            "root": "dart/fuchsia_foobar",
            "sources": [
                "dart/fuchsia_foobar/lib/foo.dart",
                "dart/fuchsia_foobar/lib/bar.dart"
            ],
            "deps": [
                "fuchsia_raboof"
            ],
            "fidl_deps": [
                "fuchsia.foobar",
                "fuchsia.raboof"
            ],
            "third_party_deps": [
                {
                    "name": "meta",
                    "version": "0.1.1"
                }
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = DartLibrary,
        data = r#"
        {
            "name": "fuchsia_foobar",
            "type": "dart_library",
            "root": "dart/fuchsia_foobar",
            "sources": [],
            "deps": [
                "fuchsia_raboof"
            ],
            "fidl_deps": [
                "fuchsia.foobar",
                "fuchsia.raboof"
            ],
            "third_party_deps": [
                {
                    "name": "meta",
                    "version": "0.1.1"
                }
            ]
        }
        "#,
        // Sources are empty.
        valid = false,
    }
}
