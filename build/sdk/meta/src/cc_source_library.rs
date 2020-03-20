// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::{BanjoLibraryName, CcLibraryName, ElementType, FidlLibraryName, File};
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct CcSourceLibrary {
    pub name: CcLibraryName,
    pub root: File,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub sources: Vec<File>,
    pub headers: Vec<File>,
    pub include_dir: File,
    pub deps: Vec<CcLibraryName>,
    pub fidl_deps: Vec<FidlLibraryName>,
    pub banjo_deps: Vec<BanjoLibraryName>,
}

impl JsonObject for CcSourceLibrary {
    fn get_schema() -> &'static str {
        include_str!("../cc_source_library.json")
    }
}

#[cfg(test)]
mod tests {
    use super::CcSourceLibrary;

    test_validation! {
        name = test_validation,
        kind = CcSourceLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "cc_source_library",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "sources": [
                "pkg/foobar/one.cc",
                "pkg/foobar/two.cc"
            ],
            "headers": [
                "pkg/foobar/include/foobar.h"
            ],
            "include_dir": "pkg/foobar/include",
            "banjo_deps": [],
            "fidl_deps": [
                "foo.bar"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = CcSourceLibrary,
        data = r#"
        {
            "name": "foobar",
            "type": "cc_source_library",
            "root": "pkg/foobar",
            "deps": [
                "raboof"
            ],
            "sources": [
                "pkg/foobar/one.cc",
                "pkg/foobar/two.cc"
            ],
            "headers": [],
            "include_dir": "pkg/foobar/include",
            "banjo_deps": [],
            "fidl_deps": [
                "foo.bar"
            ]
        }
        "#,
        // Headers are empty.
        valid = false,
    }
}
