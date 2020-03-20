// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

use crate::common::ElementType;
use crate::json::JsonObject;

#[derive(Serialize, Deserialize, Debug, Clone, Eq, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Documentation {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: ElementType,
    pub docs: Vec<String>,
}

impl JsonObject for Documentation {
    fn get_schema() -> &'static str {
        include_str!("../documentation.json")
    }
}

#[cfg(test)]
mod tests {
    use super::Documentation;

    test_validation! {
        name = test_validation,
        kind = Documentation,
        data = r#"
        {
            "name": "foobar",
            "type": "documentation",
            "docs": [
                "docs/foobar.md"
            ]
        }
        "#,
        valid = true,
    }

    test_validation! {
        name = test_validation_invalid,
        kind = Documentation,
        data = r#"
        {
            "name": "foobar",
            "type": "documentation",
            "docs": []
        }
        "#,
        // Docs are empty.
        valid = false,
    }
}
